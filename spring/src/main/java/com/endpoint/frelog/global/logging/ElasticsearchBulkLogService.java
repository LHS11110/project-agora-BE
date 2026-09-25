package com.endpoint.frelog.global.logging;

import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.annotation.PreDestroy;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;
import org.springframework.web.client.RestClientResponseException;

import java.time.Instant;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.LinkedBlockingDeque;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.ReentrantLock;

/** Buffers append-only application logs and operational events for Elasticsearch _bulk. */
@Component
public class ElasticsearchBulkLogService {
    private static final Logger log = LoggerFactory.getLogger(ElasticsearchBulkLogService.class);
    private static final int QUEUE_CAPACITY = 10_000;
    private static final int MAX_BATCHES_PER_FLUSH = 10;

    private final RestClient restClient;
    private final ElasticsearchProperties properties;
    private final ObjectMapper objectMapper;
    private final LinkedBlockingDeque<LogEvent> queue = new LinkedBlockingDeque<>(QUEUE_CAPACITY);
    private final ReentrantLock flushLock = new ReentrantLock();
    private final ConcurrentHashMap<String, Boolean> availability = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, String> primaries = new ConcurrentHashMap<>();
    private final AtomicBoolean overflowWarned = new AtomicBoolean();
    private final int batchSize;
    private final String instance;
    private final boolean enabled;

    public ElasticsearchBulkLogService(
            @Qualifier("elasticsearchLogRestClient") RestClient restClient,
            ElasticsearchProperties properties,
            ObjectMapper objectMapper,
            @Value("${app.elasticsearch.logs.batch-size:100}") int batchSize) {
        this.restClient = restClient;
        this.properties = properties;
        this.objectMapper = objectMapper;
        this.batchSize = Math.max(1, Math.min(batchSize, 1000));
        this.instance = System.getenv().getOrDefault("HOSTNAME", "unknown");
        this.enabled = properties.getLogIndex() != null && !properties.getLogIndex().isBlank()
                && properties.getLogUsername() != null && !properties.getLogUsername().isBlank()
                && properties.getLogPassword() != null && !properties.getLogPassword().isBlank();
        if (!enabled) {
            log.error("Elasticsearch application log writer is disabled. Configure ES_LOG_INDEX, ES_LOG_USER_NAME and ES_LOG_USER_PASSWORD.");
        }
    }

    public void record(String component, String event, String level, String message, Map<String, ?> details) {
        if (!enabled) return;
        Map<String, Object> document = new LinkedHashMap<>();
        document.put("@timestamp", Instant.now().toString());
        document.put("event_id", UUID.randomUUID().toString());
        document.put("service", "agora-spring");
        document.put("instance", instance);
        document.put("component", component);
        document.put("event", event);
        document.put("level", level);
        document.put("message", message);
        if (details != null && !details.isEmpty()) document.put("details", details);
        LogEvent item = new LogEvent((String) document.get("event_id"), document);
        if (!queue.offerLast(item)) {
            if (overflowWarned.compareAndSet(false, true)) {
                log.error("Elasticsearch log queue is full; new operational events are being dropped");
            }
        }
    }

    public void reportAvailability(String component, boolean healthy, Map<String, ?> details) {
        Boolean previous = availability.put(component, healthy);
        if (previous == null) {
            if (!healthy) record(component, "storage_unavailable", "ERROR",
                    component + " became unavailable", details);
            return;
        }
        if (previous == healthy) return;
        if (healthy) {
            record(component, "storage_recovered", "INFO", component + " connection recovered", details);
        } else {
            record(component, "storage_unavailable", "ERROR", component + " became unavailable", details);
        }
    }

    public void reportPrimaryChange(String component, String primary) {
        if (primary == null || primary.isBlank()) return;
        String previous = primaries.put(component, primary);
        if (previous != null && !previous.equals(primary)) {
            record(component, "primary_changed", "WARN",
                    component + " primary changed after failover",
                    Map.of("previous_primary", previous, "current_primary", primary));
        }
    }

    @org.springframework.scheduling.annotation.Scheduled(
            fixedDelayString = "${app.elasticsearch.logs.flush-interval-ms:1000}")
    public void flushScheduled() {
        flushBatches(MAX_BATCHES_PER_FLUSH);
    }

    @PreDestroy
    public void flushOnShutdown() {
        flushBatches(MAX_BATCHES_PER_FLUSH);
    }

    private void flushBatches(int maxBatches) {
        if (!enabled || !flushLock.tryLock()) return;
        try {
            for (int batchNumber = 0; batchNumber < maxBatches; batchNumber++) {
                List<LogEvent> batch = drainBatch();
                if (batch.isEmpty()) break;
                if (!sendBatch(batch)) {
                    requeueBatch(batch);
                    break;
                }
            }
            if (queue.size() < QUEUE_CAPACITY / 2) overflowWarned.set(false);
        } finally {
            flushLock.unlock();
        }
    }

    private List<LogEvent> drainBatch() {
        List<LogEvent> batch = new ArrayList<>(batchSize);
        queue.drainTo(batch, batchSize);
        return batch;
    }

    private void requeueBatch(List<LogEvent> batch) {
        boolean droppedNewerEvents = false;
        for (int i = batch.size() - 1; i >= 0; i--) {
            while (!queue.offerFirst(batch.get(i))) {
                queue.pollLast();
                droppedNewerEvents = true;
            }
        }
        if (droppedNewerEvents && overflowWarned.compareAndSet(false, true)) {
            log.error("Elasticsearch log queue is full during retry; newer events are being dropped");
        }
    }

    private boolean sendBatch(List<LogEvent> batch) {
        StringBuilder ndjson = new StringBuilder();
        try {
            for (LogEvent event : batch) {
                ndjson.append(objectMapper.writeValueAsString(Map.of("create", Map.of("_id", event.id())))).append('\n');
                ndjson.append(objectMapper.writeValueAsString(event.document())).append('\n');
            }
        } catch (Exception e) {
            log.error("Could not serialize {} Elasticsearch log events", batch.size(), e);
            return false;
        }

        for (int attempt = 0; attempt < 3; attempt++) {
            try {
                String response = restClient.post()
                        .uri("/{index}/_bulk?refresh=false", properties.getLogIndex())
                        .contentType(MediaType.parseMediaType("application/x-ndjson"))
                        .body(ndjson.toString())
                        .retrieve()
                        .body(String.class);
                if (bulkBatchAccepted(response, batch.size())) return true;
                if (attempt == 2) {
                    log.error("Elasticsearch rejected part of an append-only log batch of {} events", batch.size());
                    return false;
                }
            } catch (RestClientResponseException e) {
                int status = e.getStatusCode().value();
                if ((status < 500 && status != 429) || attempt == 2) {
                    log.error("Elasticsearch log bulk request failed with HTTP {}", status);
                    return false;
                }
            } catch (Exception e) {
                if (attempt == 2) {
                    log.error("Elasticsearch log bulk request failed after retries: {}", e.getClass().getSimpleName());
                    return false;
                }
            }
            try {
                Thread.sleep(100L * (attempt + 1));
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        return false;
    }

    private boolean bulkBatchAccepted(String response, int expectedCount) {
        try {
            JsonNode root = objectMapper.readTree(response);
            JsonNode items = root.path("items");
            if (!items.isArray() || items.size() != expectedCount) return false;
            for (JsonNode item : items) {
                JsonNode result = item.path("create");
                int status = result.path("status").asInt(0);
                // A replay after an uncertain HTTP outcome finds the same stable
                // _id. Treat create conflicts as already delivered.
                if (status != 200 && status != 201 && status != 409) return false;
            }
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private record LogEvent(String id, Map<String, Object> document) {}
}
