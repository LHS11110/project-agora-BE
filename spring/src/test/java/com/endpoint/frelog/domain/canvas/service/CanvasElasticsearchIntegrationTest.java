package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasDocumentRequest;
import com.endpoint.frelog.global.config.ElasticsearchConfig;
import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.springframework.web.client.RestClient;

import java.util.List;
import java.util.Map;
import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class CanvasElasticsearchIntegrationTest {

    private CanvasElasticsearchService service;
    private final String testCanvasName = "Agora-Integration-Test-Canvas";
    private final Integer testCanvasId = 9876;

    @BeforeEach
    void setUp() {
        ElasticsearchProperties props = new ElasticsearchProperties();
        props.setHost("127.0.0.1");
        props.setPort(9200);
        props.setIndex("canvas");
        props.setUsername("agora_user");
        props.setPassword("AgoraUserSecret@Passw0rd!2026");
        props.setFailOnError(true);

        ElasticsearchConfig config = new ElasticsearchConfig(props);
        RestClient restClient = config.elasticsearchRestClient();
        ObjectMapper objectMapper = config.objectMapper();

        service = new CanvasElasticsearchService(restClient, props, objectMapper);

        // 로컬에 Elasticsearch 도커가 떠있는 경우에만 통합 테스트 실행
        assumeTrue(service.isAvailable(), "Elasticsearch cluster is not reachable on localhost:9200");
    }

    @AfterEach
    void tearDown() {
        if (service != null && service.isAvailable()) {
            service.deleteCanvas(testCanvasId, testCanvasName);
        }
    }

    @Test
    @DisplayName("실제 실행 중인 Elasticsearch에 캔버스 문서 색인, 조회, 수정, 삭제 일괄 검증")
    void fullElasticsearchCrudLifecycle() {
        // 1. Create (저장)
        CanvasDocument newDoc = new CanvasDocument(
                testCanvasName,
                testCanvasId,
                1001L,
                "secret_hash_value",
                "default"
        );
        newDoc.setPeoples(List.of(1001L, 1002L));
        newDoc.setInnerGroup(Map.of(
                "admin-group", List.of(1001L),
                "default", List.of(1002L)
        ));
        newDoc.setItems(Map.of(
                "item-1", Map.of(
                        "item-id", 1,
                        "type", 10,
                        "pos", List.of(120.5, 340.0),
                        "permission", Map.of("admin-group", 7, "default", 3)
                )
        ));

        boolean saved = service.saveCanvas(newDoc);
        assertThat(saved).isTrue();

        // 2. Read by Name (단건 조회)
        Optional<CanvasDocument> byName = service.getCanvasDocumentByName(testCanvasName);
        assertThat(byName).isPresent();
        assertThat(byName.get().getCanvasName()).isEqualTo(testCanvasName);
        assertThat(byName.get().getCanvasId()).isEqualTo(testCanvasId);
        assertThat(byName.get().getCanvasPassword()).isEqualTo("secret_hash_value");
        assertThat(byName.get().getPeoples()).contains(1001L, 1002L);

        // 3. Read by CanvasId (term 검색)
        Optional<CanvasDocument> byId = service.getCanvasDocumentById(testCanvasId);
        assertThat(byId).isPresent();
        assertThat(byId.get().getCanvasName()).isEqualTo(testCanvasName);

        // 4. Update (비밀번호 및 그룹 갱신)
        UpdateCanvasDocumentRequest updateReq = new UpdateCanvasDocumentRequest(
                "null", // password clear
                List.of(1001L, 1002L, 1003L),
                null,
                null,
                "team-group"
        );

        Optional<CanvasDocument> updated = service.updateCanvasDocument(testCanvasId, testCanvasName, 1001L, updateReq);
        assertThat(updated).isPresent();
        assertThat(updated.get().getCanvasPassword()).isNull();
        assertThat(updated.get().getPeoples()).contains(1003L);
        assertThat(updated.get().getInitGroup()).isEqualTo("team-group");

        // 5. Delete (삭제 및 클린업)
        boolean deleted = service.deleteCanvas(testCanvasId, testCanvasName);
        assertThat(deleted).isTrue();

        // 6. Verify Deletion
        Optional<CanvasDocument> afterDelete = service.getCanvasDocumentByName(testCanvasName);
        assertThat(afterDelete).isEmpty();
    }
}
