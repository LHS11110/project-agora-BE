package com.endpoint.frelog.scheduler;

import com.endpoint.frelog.domain.elasticsearch.LogDocument;
import com.endpoint.frelog.domain.jpa.LogEntity;
import com.endpoint.frelog.dto.LogMessageDto;
import com.endpoint.frelog.repository.elasticsearch.LogElasticsearchRepository;
import com.endpoint.frelog.repository.jpa.LogJpaRepository;
import com.endpoint.frelog.service.LogQueueService;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;
import org.springframework.transaction.annotation.Transactional;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Slf4j
@Component
@RequiredArgsConstructor
public class LogBatchScheduler {

    private final LogQueueService logQueueService;
    private final LogJpaRepository logJpaRepository;
    private final LogElasticsearchRepository logElasticsearchRepository;
    private final ObjectMapper objectMapper;

    @Value("${app.batch.queue.chunk-size:100}")
    private long chunkSize;

    /**
     * 일정 주기(기본 5초)마다 Redis 큐에서 데이터를 가져와 MS SQL과 Elasticsearch에 일괄 적재
     */
    @Scheduled(fixedDelayString = "${app.batch.scheduler.fixed-delay:5000}")
    public void processPendingLogs() {
        long currentQueueSize = logQueueService.getQueueSize();
        if (currentQueueSize == 0) {
            return;
        }

        log.info("Starting batch sync. Current Redis queue size: {}", currentQueueSize);

        List<LogMessageDto> batch = logQueueService.dequeueBatch(chunkSize);
        if (batch.isEmpty()) {
            return;
        }

        try {
            // 1. MS SQL (JPA) 일괄 저장
            saveToMsSql(batch);

            // 2. Elasticsearch 일괄 저장
            saveToElasticsearch(batch);

            log.info("Successfully synced {} log items to MS SQL and Elasticsearch.", batch.size());
        } catch (Exception e) {
            log.error("Error occurred while saving batch logs. Re-queuing failed items or saving to DLQ...", e);
            // 에러 발생 시 처리 (필요에 따라 복구 큐 재삽입 등)
            for (LogMessageDto item : batch) {
                logQueueService.enqueue(item);
            }
        }
    }

    @Transactional
    protected void saveToMsSql(List<LogMessageDto> batch) {
        List<LogEntity> entities = new ArrayList<>();

        for (LogMessageDto dto : batch) {
            String metadataJson = null;
            if (dto.getMetadata() != null && !dto.getMetadata().isEmpty()) {
                try {
                    metadataJson = objectMapper.writeValueAsString(dto.getMetadata());
                } catch (JsonProcessingException e) {
                    log.warn("Failed to serialize metadata for service {}: {}", dto.getServiceName(), e.getMessage());
                }
            }

            LogEntity entity = LogEntity.builder()
                    .serviceName(dto.getServiceName())
                    .level(dto.getLevel())
                    .message(dto.getMessage())
                    .timestamp(dto.getTimestamp())
                    .metadata(metadataJson)
                    .build();

            entities.add(entity);
        }

        logJpaRepository.saveAll(entities);
    }

    protected void saveToElasticsearch(List<LogMessageDto> batch) {
        List<LogDocument> documents = new ArrayList<>();

        for (LogMessageDto dto : batch) {
            LogDocument doc = LogDocument.builder()
                    .id(UUID.randomUUID().toString())
                    .serviceName(dto.getServiceName())
                    .level(dto.getLevel())
                    .message(dto.getMessage())
                    .timestamp(dto.getTimestamp())
                    .metadata(dto.getMetadata())
                    .build();

            documents.add(doc);
        }

        logElasticsearchRepository.saveAll(documents);
    }
}
