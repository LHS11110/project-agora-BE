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

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

@Service
public class CanvasElasticsearchService {

    private static final Logger log = LoggerFactory.getLogger(CanvasElasticsearchService.class);

    private final RestClient restClient;
    private final ElasticsearchProperties properties;
    private final ObjectMapper objectMapper;
    private volatile boolean indexChecked = false;

    public CanvasElasticsearchService(
            @Qualifier("elasticsearchRestClient") RestClient restClient,
            ElasticsearchProperties properties,
            ObjectMapper objectMapper) {
        this.restClient = restClient;
        this.properties = properties;
        this.objectMapper = objectMapper;
    }

    /**
     * Elasticsearch 서버 핑 및 가용성 확인
     */
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

    /**
     * canvas 인덱스 존재 여부 확인 및 부재 시 스키마 매핑 자동 생성
     */
    public synchronized boolean ensureIndex() {
        if (indexChecked) {
            return true;
        }
        try {
            restClient.get()
                    .uri("/{index}", properties.getIndex())
                    .retrieve()
                    .toBodilessEntity();
            indexChecked = true;
            return true;
        } catch (HttpClientErrorException.NotFound e) {
            log.info("Elasticsearch 인덱스 '{}'가 존재하지 않아 자동 생성을 시도합니다.", properties.getIndex());
            createCanvasIndex();
            indexChecked = true;
            return true;
        } catch (Exception e) {
            log.warn("Elasticsearch 인덱스 확인/생성 중 오류: {}", e.getMessage());
            if (properties.isFailOnError()) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "Elasticsearch 인덱스 확인 실패: " + e.getMessage());
            }
            return false;
        }
    }

    private void createCanvasIndex() {
        String mappingJson = """
        {
          "settings": {
            "number_of_shards": 1,
            "number_of_replicas": 0
          },
          "mappings": {
            "dynamic_templates": [
              {
                "inner_group_uids": {
                  "path_match": "inner-group.*",
                  "mapping": {
                    "type": "long"
                  }
                }
              },
              {
                "item_id": {
                  "path_match": "items.*.item-id",
                  "mapping": {
                    "type": "long"
                  }
                }
              },
              {
                "item_type": {
                  "path_match": "items.*.type",
                  "mapping": {
                    "type": "integer"
                  }
                }
              },
              {
                "item_pos": {
                  "path_match": "items.*.pos",
                  "mapping": {
                    "type": "float"
                  }
                }
              },
              {
                "item_permission": {
                  "path_match": "items.*.permission.*",
                  "mapping": {
                    "type": "byte"
                  }
                }
              }
            ],
            "properties": {
              "canvas-name": {
                "type": "text",
                "fields": {
                  "keyword": {
                    "type": "keyword",
                    "ignore_above": 256
                  }
                }
              },
              "canvas-id": {
                "type": "long"
              },
              "admin": {
                "type": "long"
              },
              "canvas-password": {
                "type": "keyword"
              },
              "peoples": {
                "type": "long"
              },
              "inner-group": {
                "type": "object"
              },
              "items": {
                "type": "object"
              },
              "init-group": {
                "type": "keyword"
              }
            }
          }
        }
        """;

        try {
            restClient.put()
                    .uri("/{index}", properties.getIndex())
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(mappingJson)
                    .retrieve()
                    .toBodilessEntity();
            log.info("Elasticsearch 인덱스 '{}' 매핑 생성 완료", properties.getIndex());
        } catch (Exception e) {
            log.warn("Elasticsearch 인덱스 생성 실패: {}", e.getMessage());
            if (properties.isFailOnError()) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "Elasticsearch 인덱스 생성 실패: " + e.getMessage());
            }
        }
    }

    /**
     * 캔버스 도큐먼트 저장 (기본키: canvas-name)
     */
    public boolean saveCanvas(CanvasDocument document) {
        try {
            ensureIndex();
            String docId = document.getCanvasName();
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
            ensureIndex();
            String rawJson = restClient.get()
                    .uri("/{index}/_doc/{id}", properties.getIndex(), canvasName)
                    .retrieve()
                    .body(String.class);

            if (rawJson != null) {
                JsonNode resp = objectMapper.readTree(rawJson);
                if (resp != null && resp.has("_source")) {
                    return Optional.of(objectMapper.treeToValue(resp.get("_source"), CanvasDocument.class));
                }
            }
            return Optional.empty();
        } catch (HttpClientErrorException.NotFound e) {
            return Optional.empty();
        } catch (Exception e) {
            log.warn("Elasticsearch 캔버스 이름 '{}' 조회 실패: {}", canvasName, e.getMessage());
            if (properties.isFailOnError()) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "Elasticsearch 조회 실패: " + e.getMessage());
            }
            return Optional.empty();
        }
    }

    /**
     * canvas-id 필드로 검색하여 단건 조회
     */
    public Optional<CanvasDocument> getCanvasDocumentById(Integer canvasId) {
        try {
            ensureIndex();
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
            return Optional.empty();
        } catch (Exception e) {
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
        try {
            ensureIndex();
            Map<String, Object> queryBody = Map.of(
                    "query", Map.of("match_all", Map.of()),
                    "size", 1000
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
                    java.util.List<CanvasDocument> docs = new java.util.ArrayList<>();
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
            log.warn("Elasticsearch 캔버스 목록 조회 실패: {}", e.getMessage());
            return Collections.emptyList();
        }
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
            // 아직 ES에 도큐먼트가 색인되지 않은 경우 새 도큐먼트 생성
            doc = new CanvasDocument(fallbackCanvasName, canvasId, ownerId, request.canvasPassword(), request.initGroup());
        }

        // 요청 데이터 적용
        if (request.canvasPassword() != null) {
            doc.setCanvasPassword(request.canvasPassword().equals("null") ? null : request.canvasPassword());
        }
        if (request.peoples() != null) {
            doc.setPeoples(request.peoples());
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
        String targetName = canvasName;
        if (targetName == null && canvasId != null) {
            Optional<CanvasDocument> doc = getCanvasDocumentById(canvasId);
            if (doc.isPresent()) {
                targetName = doc.get().getCanvasName();
            }
        }

        if (targetName == null) {
            return false;
        }

        try {
            ensureIndex();
            restClient.delete()
                    .uri("/{index}/_doc/{id}?refresh=true", properties.getIndex(), targetName)
                    .retrieve()
                    .toBodilessEntity();
            log.info("캔버스 '{}' Elasticsearch 문서 삭제 완료", targetName);
            return true;
        } catch (HttpClientErrorException.NotFound e) {
            return false;
        } catch (Exception e) {
            log.warn("캔버스 '{}' Elasticsearch 삭제 실패: {}", targetName, e.getMessage());
            if (properties.isFailOnError()) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "Elasticsearch 삭제 실패: " + e.getMessage());
            }
            return false;
        }
    }
}
