package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasDocumentRequest;
import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Service;
import org.springframework.web.client.HttpClientErrorException;
import org.springframework.web.client.RestClient;
import org.springframework.web.client.RestClientResponseException;

import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;

@Service
public class CanvasElasticsearchService {

    private static final Logger log = LoggerFactory.getLogger(CanvasElasticsearchService.class);

    private final RestClient restClient;
    private final ElasticsearchProperties properties;
    private final ObjectMapper objectMapper;

    public CanvasElasticsearchService(
            @Qualifier("elasticsearchRestClient") RestClient restClient,
            ElasticsearchProperties properties,
            ObjectMapper objectMapper) {
        this.restClient = restClient;
        this.properties = properties;
        this.objectMapper = objectMapper;
    }

    public boolean isAvailable() {
        try {
            restClient.get()
                    .uri("/")
                    .retrieve()
                    .toBodilessEntity();
            return true;
        } catch (Exception e) {
            log.debug("Elasticsearch 서버 가용성 체크 실패: {}", e.getMessage());
            return false;
        }
    }

    public boolean isIndexExists() {
        try {
            restClient.head()
                    .uri("/{index}", properties.getIndex())
                    .retrieve()
                    .toBodilessEntity();
            return true;
        } catch (HttpClientErrorException.NotFound e) {
            return false;
        } catch (Exception e) {
            log.debug("Elasticsearch 인덱스 '{}' 존재 확인 실패: {}", properties.getIndex(), e.getMessage());
            return false;
        }
    }

    public boolean isIndexNotFoundException(Exception e) {
        if (e instanceof RestClientResponseException rre) {
            if (rre.getStatusCode().value() == 404) {
                String body = rre.getResponseBodyAsString();
                if (body != null) {
                    return body.contains("index_not_found_exception") || body.contains("no such index");
                }
            }
        }
        return false;
    }

    private void handleIndexNotFound(String operation, Exception e) {
        log.error("Elasticsearch 인덱스 '{}'가 존재하지 않습니다 (index_not_found_exception). {} 실패: {}",
                properties.getIndex(), operation, e.getMessage());
        if (properties.isFailOnError()) {
            throw new CustomException(
                    ErrorCode.ELASTICSEARCH_INDEX_NOT_FOUND,
                    "Elasticsearch 인덱스 '" + properties.getIndex() + "'가 존재하지 않습니다: " + e.getMessage()
            );
        }
    }

    /**
     * 캔버스 도큐먼트 저장 (docId: canvasId)
     */
    public boolean saveCanvas(CanvasDocument document) {
        try {
            String docId = String.valueOf(document.getCanvasId());
            String docJson = objectMapper.writeValueAsString(document);
            restClient.put()
                    .uri("/{index}/_doc/{id}?refresh=true", properties.getIndex(), docId)
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(docJson)
                    .retrieve()
                    .toBodilessEntity();
            log.info("캔버스 '{}'(ID: {}) Elasticsearch 저장 성공", document.getCanvasName(), document.getCanvasId());
            return true;
        } catch (Exception e) {
            if (isIndexNotFoundException(e)) {
                handleIndexNotFound("도큐먼트 색인 저장 (saveCanvas)", e);
                return false;
            }
            log.warn("캔버스 '{}' Elasticsearch 저장 실패: {}", document.getCanvasName(), e.getMessage());
            if (properties.isFailOnError()) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "Elasticsearch 색인 실패: " + e.getMessage());
            }
            return false;
        }
    }

    /**
     * 캔버스 이름으로 단건 조회
     */
    public Optional<CanvasDocument> getCanvasDocumentByName(String canvasName) {
        try {
            Map<String, Object> queryBody = Map.of(
                    "query", Map.of("term", Map.of("canvas-name.keyword", canvasName)),
                    "size", 1
            );
            String queryJson = objectMapper.writeValueAsString(queryBody);

            String rawJson = restClient.post()
                    .uri("/{index}/_search", properties.getIndex())
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(queryJson)
                    .retrieve()
                    .body(String.class);

            if (rawJson != null) {
                JsonNode resp = objectMapper.readTree(rawJson);
                if (resp != null && resp.has("hits") && resp.get("hits").has("hits")) {
                    JsonNode hits = resp.get("hits").get("hits");
                    if (hits.isArray() && !hits.isEmpty()) {
                        JsonNode source = hits.get(0).get("_source");
                        return Optional.of(objectMapper.treeToValue(source, CanvasDocument.class));
                    }
                }
            }

            // Fallback: search by ID or direct ID lookup
            String directJson = restClient.get()
                    .uri("/{index}/_doc/{id}", properties.getIndex(), canvasName)
                    .retrieve()
                    .body(String.class);
            if (directJson != null) {
                JsonNode resp = objectMapper.readTree(directJson);
                if (resp != null && resp.has("_source")) {
                    return Optional.of(objectMapper.treeToValue(resp.get("_source"), CanvasDocument.class));
                }
            }
            return Optional.empty();
        } catch (Exception e) {
            return Optional.empty();
        }
    }

    /**
     * 캔버스 이름 기반 검색 (요구사항 4.2)
     */
    public List<CanvasDocument> searchCanvasesByName(String canvasName) {
        try {
            Map<String, Object> queryBody;
            if (canvasName == null || canvasName.isBlank()) {
                queryBody = Map.of(
                        "query", Map.of("match_all", Map.of()),
                        "size", 1000
                );
            } else {
                queryBody = Map.of(
                        "query", Map.of("match", Map.of("canvas-name", canvasName)),
                        "size", 1000
                );
            }
            String queryJson = objectMapper.writeValueAsString(queryBody);

            String rawJson = restClient.post()
                    .uri("/{index}/_search", properties.getIndex())
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(queryJson)
                    .retrieve()
                    .body(String.class);

            if (rawJson != null) {
                JsonNode resp = objectMapper.readTree(rawJson);
                if (resp != null && resp.has("hits") && resp.get("hits").has("hits")) {
                    JsonNode hits = resp.get("hits").get("hits");
                    List<CanvasDocument> docs = new java.util.ArrayList<>();
                    for (JsonNode hit : hits) {
                        if (hit.has("_source")) {
                            docs.add(objectMapper.treeToValue(hit.get("_source"), CanvasDocument.class));
                        }
                    }
                    return docs;
                }
            }
            return Collections.emptyList();
        } catch (Exception e) {
            log.warn("Elasticsearch 캔버스 이름 검색 실패 ('{}'): {}", canvasName, e.getMessage());
            return Collections.emptyList();
        }
    }

    /**
     * canvas-id 필드로 검색하여 단건 조회
     */
    public Optional<CanvasDocument> getCanvasDocumentById(Integer canvasId) {
        try {
            // 1. Direct doc lookup by ID
            String rawJson = restClient.get()
                    .uri("/{index}/_doc/{id}", properties.getIndex(), String.valueOf(canvasId))
                    .retrieve()
                    .body(String.class);

            if (rawJson != null) {
                JsonNode resp = objectMapper.readTree(rawJson);
                if (resp != null && resp.has("_source")) {
                    return Optional.of(objectMapper.treeToValue(resp.get("_source"), CanvasDocument.class));
                }
            }
        } catch (Exception ignored) {
        }

        try {
            // 2. Term query search
            Map<String, Object> queryBody = Map.of(
                    "query", Map.of("term", Map.of("canvas-id", canvasId)),
                    "size", 1
            );
            String queryJson = objectMapper.writeValueAsString(queryBody);

            String rawJson = restClient.post()
                    .uri("/{index}/_search", properties.getIndex())
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(queryJson)
                    .retrieve()
                    .body(String.class);

            if (rawJson != null) {
                JsonNode resp = objectMapper.readTree(rawJson);
                if (resp != null && resp.has("hits") && resp.get("hits").has("hits")) {
                    JsonNode hits = resp.get("hits").get("hits");
                    if (hits.isArray() && !hits.isEmpty()) {
                        JsonNode source = hits.get(0).get("_source");
                        return Optional.of(objectMapper.treeToValue(source, CanvasDocument.class));
                    }
                }
            }
            return Optional.empty();
        } catch (HttpClientErrorException.NotFound e) {
            handleIndexNotFound("도큐먼트 ID 검색 (getCanvasDocumentById)", e);
            return Optional.empty();
        } catch (Exception e) {
            if (isIndexNotFoundException(e)) {
                handleIndexNotFound("도큐먼트 ID 검색 (getCanvasDocumentById)", e);
                return Optional.empty();
            }
            log.warn("Elasticsearch 캔버스 ID {} 검색 실패: {}", canvasId, e.getMessage());
            if (properties.isFailOnError()) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "Elasticsearch 검색 실패: " + e.getMessage());
            }
            return Optional.empty();
        }
    }

    /**
     * Elasticsearch 내 전체 캔버스 도큐먼트 목록 조회 (match_all)
     */
    public List<CanvasDocument> listCanvasDocuments() {
        return searchCanvasesByName(null);
    }

    /**
     * 캔버스 도큐먼트 업데이트
     */
    public Optional<CanvasDocument> updateCanvasDocument(Integer canvasId, String fallbackCanvasName, Long ownerId, UpdateCanvasDocumentRequest request) {
        Optional<CanvasDocument> existingOpt = getCanvasDocumentById(canvasId);
        if (existingOpt.isEmpty() && fallbackCanvasName != null) {
            existingOpt = getCanvasDocumentByName(fallbackCanvasName);
        }

        CanvasDocument doc;
        if (existingOpt.isPresent()) {
            doc = existingOpt.get();
        } else {
            doc = new CanvasDocument(fallbackCanvasName, canvasId, ownerId, request.canvasPassword(), request.initGroup());
        }

        if (request.canvasPassword() != null) {
            doc.setCanvasPasswordHash(request.canvasPassword().equals("null") ? null : request.canvasPassword());
        }
        if (request.peoples() != null) {
            doc.setPeople(request.peoples());
        }
        if (request.innerGroup() != null) {
            doc.setInnerGroup(request.innerGroup());
        }
        if (request.items() != null) {
            doc.setItems(request.items());
        }
        if (request.initGroup() != null && !request.initGroup().isBlank()) {
            doc.setInitGroup(request.initGroup());
        }

        boolean saved = saveCanvas(doc);
        return saved ? Optional.of(doc) : Optional.empty();
    }

    /**
     * 캔버스 도큐먼트 삭제
     */
    public boolean deleteCanvas(Integer canvasId, String canvasName) {
        boolean deleted = false;
        if (canvasId != null) {
            try {
                restClient.delete()
                        .uri("/{index}/_doc/{id}?refresh=true", properties.getIndex(), String.valueOf(canvasId))
                        .retrieve()
                        .toBodilessEntity();
                log.info("캔버스 #{} Elasticsearch 문서 삭제 완료", canvasId);
                deleted = true;
            } catch (Exception ignored) {
            }
        }

        if (canvasName != null) {
            try {
                restClient.delete()
                        .uri("/{index}/_doc/{id}?refresh=true", properties.getIndex(), canvasName)
                        .retrieve()
                        .toBodilessEntity();
                deleted = true;
            } catch (Exception ignored) {
            }
        }

        return deleted;
    }
}
