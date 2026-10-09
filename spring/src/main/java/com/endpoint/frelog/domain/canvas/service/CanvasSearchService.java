package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/** License-independent weighted RRF over Korean BM25 and E5 approximate kNN. */
@Service
public class CanvasSearchService {
    private static final Logger log = LoggerFactory.getLogger(CanvasSearchService.class);
    private static final List<String> SUMMARY_FIELDS = List.of("canvas-id", "canvas-name", "description", "people");
    private final RestClient es;
    private final ElasticsearchProperties properties;
    private final ObjectMapper mapper;
    private final CanvasEmbeddingClient embeddings;
    private final AtomicLong lastWarning = new AtomicLong();
    public CanvasSearchService(@Qualifier("elasticsearchRestClient") RestClient es, ElasticsearchProperties properties,
                              ObjectMapper mapper, CanvasEmbeddingClient embeddings) {
        this.es = es; this.properties = properties; this.mapper = mapper; this.embeddings = embeddings;
    }
    public static Map<String, Object> lexicalQuery(String query, int size) {
        return Map.of("size", size, "_source", false, "query", Map.of("bool", Map.of("minimum_should_match", 1,
            "should", List.of(
                Map.of("term", Map.of("canvas-name.keyword", Map.of("value", query, "boost", 12))),
                Map.of("match_phrase", Map.of("canvas-name", Map.of("query", query, "boost", 5))),
                Map.of("multi_match", Map.of("query", query, "fields", List.of("canvas-name^4", "description"), "type", "best_fields")),
                Map.of("multi_match", Map.of("query", query, "fields", List.of("canvas-name.typo^2", "description.typo"),
                    "fuzziness", "AUTO:3,6", "prefix_length", 0, "max_expansions", 25, "boost", .4)),
                Map.of("match", Map.of("canvas-name.compact", Map.of("query", query, "boost", 3)))
            ))));
    }
    public static Map<String, Object> vectorQuery(CanvasEmbeddingClient.Embedding embedding, int k, int candidates, double similarity) {
        return Map.of("size", k, "_source", false, "knn", Map.of("field", "embedding", "query_vector", embedding.vector(),
            "k", k, "num_candidates", candidates, "similarity", similarity,
            "filter", Map.of("term", Map.of("model-revision", embedding.revision()))));
    }
    private JsonNode request(String index, String operation, Object body) throws Exception {
        String response = es.post().uri("/{index}/{operation}", index, operation)
                .contentType(org.springframework.http.MediaType.APPLICATION_JSON).body(mapper.writeValueAsString(body))
                .retrieve().body(String.class);
        return mapper.readTree(response);
    }
    private List<String> ids(JsonNode response) {
        if (response.path("timed_out").asBoolean() || response.path("_shards").path("failed").asInt() > 0)
            throw new IllegalStateException("Incomplete search response");
        List<String> result = new ArrayList<>();
        for (JsonNode hit : response.path("hits").path("hits")) if (hit.path("_id").isTextual()) result.add(hit.path("_id").asText());
        return result;
    }
    public static List<String> fuse(List<String> lexical, List<String> canonical, List<String> semantic, int limit) {
        Map<String, Double> scores = new HashMap<>();
        List<List<String>> lists = List.of(lexical, canonical, semantic);
        for (int source = 0; source < lists.size(); source++) {
            int rank = 0;
            for (String id : lists.get(source)) scores.merge(id, (source == 0 ? 2.0 : 1.0) / (60 + ++rank), Double::sum);
        }
        return scores.keySet().stream().sorted(Comparator.<String>comparingDouble(scores::get).reversed().thenComparing(Comparator.naturalOrder()))
            .limit(limit).toList();
    }
    private void degraded() {
        long now = System.currentTimeMillis(), previous = lastWarning.get();
        if (now - previous > 60000 && lastWarning.compareAndSet(previous, now))
            log.warn("Semantic/Korean search projection unavailable; canonical keyword search remains active.");
    }
    public List<CanvasDocument> search(String rawQuery) throws Exception {
        String query = rawQuery.strip();
        if (query.length() > 512) throw new IllegalArgumentException("Search query exceeds 512 characters");
        int limit = properties.getSearchLimit(), candidates = properties.getSearchCandidates();
        List<String> lexical = List.of(), semantic = List.of();
        try { lexical = ids(request(properties.getSearchIndex(), "_search", lexicalQuery(query, limit))); }
        catch (Exception unavailable) { degraded(); }
        // Canonical BM25 includes just-saved documents before the next projection scan.
        List<String> canonical = ids(request(properties.getIndex(), "_search", Map.of("size", limit, "_source", false,
            "query", Map.of("multi_match", Map.of("query", query, "fields", List.of("canvas-name^4", "description"),
                "fuzziness", "AUTO:3,6", "max_expansions", 25)))));
        if (properties.isSemanticSearchEnabled() && embeddings != null) {
            try { semantic = ids(request(properties.getSearchIndex(), "_search", vectorQuery(embeddings.embed(query), limit,
                candidates, properties.getSearchSimilarity()))); }
            catch (Exception unavailable) { degraded(); }
        }
        List<String> ranked = fuse(lexical, canonical, semantic, limit);
        if (ranked.isEmpty()) return List.of();
        List<Map<String, Object>> docs = ranked.stream().map(id -> Map.<String, Object>of("_id", id, "_source", SUMMARY_FIELDS)).toList();
        JsonNode response = request(properties.getIndex(), "_mget", Map.of("docs", docs));
        Map<String, CanvasDocument> current = new HashMap<>();
        for (JsonNode doc : response.path("docs")) {
            if (doc.has("error")) throw new IllegalStateException("Canonical summary lookup failed");
            if (doc.path("found").asBoolean() && doc.path("_source").isObject())
                current.put(doc.path("_id").asText(), mapper.treeToValue(doc.path("_source"), CanvasDocument.class));
        }
        List<CanvasDocument> result = new ArrayList<>();
        for (String id : ranked) if (current.containsKey(id)) result.add(current.get(id));
        return result;
    }
}
