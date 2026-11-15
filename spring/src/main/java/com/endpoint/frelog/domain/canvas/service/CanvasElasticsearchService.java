package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocumentJson;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocumentJson;
import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasDocumentRequest;
import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Service;
import org.springframework.web.client.HttpClientErrorException;
import org.springframework.web.client.RestClient;
import org.springframework.web.client.RestClientResponseException;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;

@Service
public class CanvasElasticsearchService {

    private static final Logger log = LoggerFactory.getLogger(CanvasElasticsearchService.class);
    private static final int ITEMS_CHUNK_BYTES = 8190;

    private final CanvasSearchService searchService;
    private final RestClient restClient;
    private final ElasticsearchProperties properties;
    private final ObjectMapper objectMapper;

    public CanvasElasticsearchService(
            @Qualifier("elasticsearchRestClient") RestClient restClient,
            ElasticsearchProperties properties,
            ObjectMapper objectMapper) {
        this(restClient, properties, objectMapper, new CanvasSearchService(restClient, properties, objectMapper, null));
    }

    @org.springframework.beans.factory.annotation.Autowired
    public CanvasElasticsearchService(@Qualifier("elasticsearchRestClient") RestClient restClient,
            ElasticsearchProperties properties, ObjectMapper objectMapper, CanvasSearchService searchService) {
        this.restClient = restClient;
        this.properties = properties;
        this.objectMapper = objectMapper;
        this.searchService = searchService;
    }

    private String indexFor(DocumentScope scope) {
        return scope == DocumentScope.WORKSPACE ? properties.getWorkspaceIndex() : properties.getIndex();
    }

    private String encodeCanvasDocument(CanvasDocument document) throws Exception {
        return encodeDocument(DocumentScope.CANVAS, document);
    }

    private String encodeDocument(DocumentScope scope, CanvasDocument document) throws Exception {
        ObjectNode source = CanvasDocumentJson.toStorageNode(objectMapper, document, scope);
        JsonNode items = source.remove("items");
        byte[] raw = objectMapper.writeValueAsBytes(items == null ? objectMapper.createObjectNode() : items);
        ArrayNode chunks = source.putArray("items-b64");
        for (int offset = 0; offset < raw.length; offset += ITEMS_CHUNK_BYTES) {
            int end = Math.min(offset + ITEMS_CHUNK_BYTES, raw.length);
            chunks.add(Base64.getEncoder().encodeToString(Arrays.copyOfRange(raw, offset, end)));
        }
        JsonNode sessions=source.remove("agent-sessions");
        if(sessions!=null){
            if(!sessions.isObject())throw new IllegalArgumentException("Invalid agent sessions");
            byte[] data=objectMapper.writeValueAsBytes(sessions);
            ArrayNode encoded=source.putArray("agent-sessions-b64");
            for(int offset=0;offset<data.length;offset+=ITEMS_CHUNK_BYTES){
                encoded.add(Base64.getEncoder().encodeToString(Arrays.copyOfRange(data,offset,Math.min(offset+ITEMS_CHUNK_BYTES,data.length))));
            }
        }
        return objectMapper.writeValueAsString(source);
    }

    private CanvasDocument decodeCanvasDocument(JsonNode source) throws Exception {
        if (source == null || !source.isObject()) throw new IllegalArgumentException("Invalid canvas source");
        ObjectNode restored = CanvasDocumentJson.normalize(source);
        JsonNode chunks = restored.remove("items-b64");
        if (chunks != null) {
            if (!chunks.isArray() || chunks.isEmpty()) throw new IllegalArgumentException("Invalid encoded canvas items");
            ByteArrayOutputStream raw = new ByteArrayOutputStream();
            for (JsonNode chunk : chunks) {
                if (!chunk.isTextual() || chunk.textValue().length() > 4 * ((ITEMS_CHUNK_BYTES + 2) / 3)) {
                    throw new IllegalArgumentException("Invalid encoded canvas items chunk");
                }
                byte[] decoded = Base64.getDecoder().decode(chunk.textValue());
                raw.write(decoded, 0, decoded.length);
            }
            JsonNode items = objectMapper.readTree(raw.toByteArray());
            if (!items.isObject()) throw new IllegalArgumentException("Invalid canvas items");
            restored.set("items", items);
        }
        return objectMapper.treeToValue(restored, CanvasDocument.class);
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
        return isIndexExists(DocumentScope.CANVAS);
    }

    public boolean isIndexExists(DocumentScope scope) {
        String indexName = indexFor(scope);
        try {
            restClient.head()
                    .uri("/{index}", indexName)
                    .retrieve()
                    .toBodilessEntity();
            return true;
        } catch (HttpClientErrorException.NotFound e) {
            return false;
        } catch (Exception e) {
            log.debug("Elasticsearch 인덱스 '{}' 존재 확인 실패: {}", indexName, e.getMessage());
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
        handleIndexNotFound(DocumentScope.CANVAS, operation, e);
    }

    private void handleIndexNotFound(DocumentScope scope, String operation, Exception e) {
        String indexName = indexFor(scope);
        log.error("Elasticsearch 인덱스 '{}'가 존재하지 않습니다 (index_not_found_exception). {} 실패: {}",
                indexName, operation, e.getMessage());
        if (properties.isFailOnError()) {
            throw new CustomException(
                    ErrorCode.ELASTICSEARCH_INDEX_NOT_FOUND,
                    "Elasticsearch 인덱스 '" + indexName + "'가 존재하지 않습니다: " + e.getMessage()
            );
        }
    }

    /**
     * 캔버스 또는 워크스페이스 도큐먼트 저장 (docId: spaceId)
     */
    public boolean saveCanvas(CanvasDocument document) {
        return saveDocument(DocumentScope.CANVAS, document);
    }

    public boolean saveDocument(DocumentScope scope, CanvasDocument document) {
        String indexName = indexFor(scope);
        try {
            document.setCanvasPasswordHash(CanvasPasswords.normalizeStoredHash(document.getCanvasPasswordHash()));
            String docId = String.valueOf(document.getCanvasId());
            String docJson = encodeDocument(scope, document);
            restClient.put()
                    .uri("/{index}/_doc/{id}?refresh=true", indexName, docId)
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(docJson)
                    .retrieve()
                    .toBodilessEntity();
            log.info("{} '{}'(ID: {}) Elasticsearch({}) 저장 성공", scope.wireName(), document.getCanvasName(), document.getCanvasId(), indexName);
            return true;
        } catch (Exception e) {
            if (isIndexNotFoundException(e)) {
                handleIndexNotFound(scope, "도큐먼트 색인 저장 (saveDocument)", e);
                return false;
            }
            log.warn("{} '{}' Elasticsearch 저장 실패: {}", scope.wireName(), document.getCanvasName(), e.getMessage());
            if (properties.isFailOnError()) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "Elasticsearch 색인 실패: " + e.getMessage());
            }
            return false;
        }
    }

    /** Update only settings fields so an item snapshot is never written over newer items. */
    public void patchCanvasFields(Integer canvasId, Map<String, Object> fields) {
        patchFields(DocumentScope.CANVAS, canvasId, fields);
    }

    public void patchFields(DocumentScope scope, Integer id, Map<String, Object> fields) {
        String indexName = indexFor(scope);
        try {
            Map<String, Object> normalized = new java.util.HashMap<>(fields);
            if (normalized.containsKey("canvas-password-hash")) {
                normalized.put("canvas-password-hash", CanvasPasswords.normalizeStoredHash((String) normalized.get("canvas-password-hash")));
            }
            if (scope == DocumentScope.WORKSPACE && normalized.containsKey("canvas-name")) {
                normalized.put("workspace-name", normalized.remove("canvas-name"));
            }
            restClient.post()
                    .uri("/{index}/_update/{id}?refresh=true&retry_on_conflict=3", indexName, String.valueOf(id))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(objectMapper.writeValueAsString(Map.of("doc", normalized)))
                    .retrieve()
                    .toBodilessEntity();
        } catch (Exception e) {
            log.warn("{} #{} 설정 저장 실패: {}", scope.wireName(), id, e.getMessage());
            throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "설정 저장에 실패했습니다.");
        }
    }

    /**
     * 이름으로 단건 조회
     */
    public Optional<CanvasDocument> getCanvasDocumentByName(String canvasName) {
        return getDocumentByName(DocumentScope.CANVAS, canvasName);
    }

    public Optional<CanvasDocument> getDocumentByName(DocumentScope scope, String name) {
        String indexName = indexFor(scope);
        try {
            Map<String, Object> queryBody = Map.of(
                    "query", Map.of("term", Map.of(scope.nameField() + ".keyword", name)),
                    "size", 1
            );
            String queryJson = objectMapper.writeValueAsString(queryBody);

            String rawJson = restClient.post()
                    .uri("/{index}/_search", indexName)
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
                        return Optional.of(decodeCanvasDocument(source));
                    }
                }
            }

            // Fallback: direct ID lookup
            String directJson = restClient.get()
                    .uri("/{index}/_doc/{id}", indexName, name)
                    .retrieve()
                    .body(String.class);
            if (directJson != null) {
                JsonNode resp = objectMapper.readTree(directJson);
                if (resp != null && resp.has("_source")) {
                    return Optional.of(decodeCanvasDocument(resp.get("_source")));
                }
            }
            return Optional.empty();
        } catch (Exception e) {
            return Optional.empty();
        }
    }

    /**
     * 이름 기반 검색 (Canvas는 canvas-search 인덱스, Workspace는 workspace 인덱스 전용 조회)
     */
    public List<CanvasDocument> searchCanvasesByName(String canvasName) {
        return searchDocumentsByName(DocumentScope.CANVAS, canvasName);
    }

    public List<CanvasDocument> searchDocumentsByName(DocumentScope scope, String name) {
        String indexName = indexFor(scope);
        try {
            if (scope == DocumentScope.CANVAS && name != null && !name.isBlank()) return searchService.search(name);
            Map<String, Object> query = (name == null || name.isBlank())
                    ? Map.of("match_all", Map.of())
                    : Map.of("multi_match", Map.of(
                            "query", name,
                            "fields", List.of(scope.nameField() + "^2", "description"),
                            "type", "phrase_prefix"));
            Map<String, Object> queryBody = Map.of(
                    "query", query,
                    "_source", List.of(scope.idField(), scope.nameField(), "canvas-id", "canvas-name", "description", "people", "admin-user-id", "visibility", "inner-group", "init-group", "canvas-password-hash", "group-permissions", "group-bits"),
                    "size", 1000
            );
            String queryJson = objectMapper.writeValueAsString(queryBody);

            String rawJson = restClient.post()
                    .uri("/{index}/_search", indexName)
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
                            docs.add(decodeCanvasDocument(hit.get("_source")));
                        }
                    }
                    return docs;
                }
            }
            return Collections.emptyList();
        } catch (Exception e) {
            log.warn("Elasticsearch {} 검색 실패: {}", scope.wireName(), e.getClass().getSimpleName());
            return Collections.emptyList();
        }
    }

    /**
     * 식별자 필드로 검색하여 단건 조회
     */
    public Optional<CanvasDocument> getCanvasDocumentById(Integer canvasId) {
        return getDocumentById(DocumentScope.CANVAS, canvasId);
    }

    public Optional<CanvasDocument> getDocumentById(DocumentScope scope, Integer id) {
        String indexName = indexFor(scope);
        try {
            // 1. Direct doc lookup by ID
            String rawJson = restClient.get()
                    .uri("/{index}/_doc/{id}", indexName, String.valueOf(id))
                    .retrieve()
                    .body(String.class);

            if (rawJson != null) {
                JsonNode resp = objectMapper.readTree(rawJson);
                if (resp != null && resp.has("_source")) {
                    return Optional.of(decodeCanvasDocument(resp.get("_source")));
                }
            }
        } catch (HttpClientErrorException.NotFound e) {
            if (isIndexNotFoundException(e)) {
                throw canvasReadFailure("문서 ID 조회", e);
            }
            // A missing document can still have a legacy document ID, so try the field query below.
        } catch (Exception e) {
            throw canvasReadFailure("문서 ID 조회", e);
        }

        try {
            // 2. Term query search
            Map<String, Object> queryBody = Map.of(
                    "query", Map.of("term", Map.of(scope.idField(), id)),
                    "size", 1
            );
            String queryJson = objectMapper.writeValueAsString(queryBody);

            String rawJson = restClient.post()
                    .uri("/{index}/_search", indexName)
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
                        return Optional.of(decodeCanvasDocument(source));
                    }
                }
            }
            return Optional.empty();
        } catch (HttpClientErrorException.NotFound e) {
            throw canvasReadFailure(scope.idField() + " 검색", e);
        } catch (Exception e) {
            throw canvasReadFailure(scope.idField() + " 검색", e);
        }
    }

    private CustomException canvasReadFailure(String operation, Exception e) {
        if (isIndexNotFoundException(e)) {
            log.error("Elasticsearch 캔버스 인덱스 '{}'를 읽을 수 없습니다 ({}).", properties.getIndex(), operation);
            return new CustomException(
                    ErrorCode.ELASTICSEARCH_INDEX_NOT_FOUND,
                    "Elasticsearch 캔버스 인덱스를 읽을 수 없습니다."
            );
        }

        log.error("Elasticsearch 캔버스 조회 실패 ({}): {}", operation, e.getMessage());
        return new CustomException(
                ErrorCode.INTERNAL_SERVER_ERROR,
                "캔버스 저장소 조회에 실패했습니다. 잠시 후 다시 시도해 주세요."
        );
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
        return deleteDocument(DocumentScope.CANVAS, canvasId, canvasName);
    }

    public boolean deleteDocument(DocumentScope scope, Integer id, String name) {
        if (id == null) return false;
        String indexName = indexFor(scope);
        try {
            restClient.delete()
                    .uri("/{index}/_doc/{id}?refresh=true", indexName, String.valueOf(id))
                    .retrieve()
                    .toBodilessEntity();
            log.info("{} #{} Elasticsearch 문서({}) 삭제 완료", scope.wireName(), id, indexName);
        } catch (Exception e) {
            log.error("{} #{} Elasticsearch 문서 삭제 실패", scope.wireName(), id, e);
            return false;
        }

        if (name != null && !name.equals(String.valueOf(id))) {
            try {
                restClient.delete()
                        .uri("/{index}/_doc/{id}?refresh=true", indexName, name)
                        .retrieve()
                        .toBodilessEntity();
            } catch (Exception e) {
                log.warn("{} #{}의 이전 이름 기반 Elasticsearch 문서 삭제 실패: {}", scope.wireName(), id, e.getMessage());
            }
        }

        return true;
    }
}
