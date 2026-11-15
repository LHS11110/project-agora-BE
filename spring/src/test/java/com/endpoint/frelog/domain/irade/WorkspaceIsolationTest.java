package com.endpoint.frelog.domain.irade;

import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocumentJson;
import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.canvas.repository.WorkspaceInfoRepository;
import com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupQueue;
import com.endpoint.frelog.domain.canvas.service.CanvasElasticsearchService;
import com.endpoint.frelog.domain.loadbalancer.repository.RedisInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.service.LoadBalancerService;
import com.endpoint.frelog.global.config.DatabaseProperties;
import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.junit.jupiter.api.Test;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.web.client.RestClient;

import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.List;
import java.util.Map;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class WorkspaceIsolationTest {

    private static final String SECRET = "this-is-a-very-long-secret-key-for-testing-jwt-token-provider-2026";
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Test
    void documentScopeSeparatesKeysFieldsAndRuntimeTargetIds() {
        assertThat(DocumentScope.CANVAS.redisKey(1)).isEqualTo("canvas:1");
        assertThat(DocumentScope.WORKSPACE.redisKey(1)).isEqualTo("workspace:1");
        assertThat(DocumentScope.CANVAS.idField()).isEqualTo("canvas-id");
        assertThat(DocumentScope.WORKSPACE.idField()).isEqualTo("workspace-id");
        assertThat(DocumentScope.CANVAS.runtimeTargetId(42)).isEqualTo(42);
        assertThat(DocumentScope.WORKSPACE.runtimeTargetId(42)).isEqualTo(1_000_000_042);
        assertThatThrownBy(() -> DocumentScope.fromWire("unknown"))
                .isInstanceOf(IllegalArgumentException.class);
    }

    @Test
    void jwtAccessTokensAndPasswordGrantsDoNotCrossScopes() throws Exception {
        JwtTokenProvider provider = new JwtTokenProvider(SECRET, 60_000);

        String workspaceToken = provider.createWorkspaceAccessToken("alice", 1001, 1, "127.0.0.1", "hash", 3L);
        String canvasToken = provider.createCanvasAccessToken("alice", 1001, 1, "127.0.0.1", "hash", 3L);

        JsonNode workspaceClaims = objectMapper.readTree(
                Base64.getUrlDecoder().decode(workspaceToken.split("\\.")[1]));
        JsonNode canvasClaims = objectMapper.readTree(
                Base64.getUrlDecoder().decode(canvasToken.split("\\.")[1]));

        assertThat(workspaceClaims.path("sub").asText()).isEqualTo("workspace-access");
        assertThat(workspaceClaims.path("workspaceId").asInt()).isEqualTo(1);
        assertThat(workspaceClaims.has("canvasId")).isFalse();

        assertThat(canvasClaims.path("sub").asText()).isEqualTo("canvas-access");
        assertThat(canvasClaims.path("canvasId").asInt()).isEqualTo(1);
        assertThat(canvasClaims.has("workspaceId")).isFalse();

        String workspaceGrant = provider.createPasswordGrant(DocumentScope.WORKSPACE, 7L, 1, 2L);
        String canvasGrant = provider.createCanvasPasswordGrant(7L, 1, 2L);

        assertThat(provider.validatePasswordGrant(DocumentScope.WORKSPACE, workspaceGrant, 7L, 1, 2L)).isTrue();
        assertThat(provider.validatePasswordGrant(DocumentScope.CANVAS, workspaceGrant, 7L, 1, 2L)).isFalse();
        assertThat(provider.validateCanvasPasswordGrant(workspaceGrant, 7L, 1, 2L)).isFalse();

        assertThat(provider.validateCanvasPasswordGrant(canvasGrant, 7L, 1, 2L)).isTrue();
        assertThat(provider.validatePasswordGrant(DocumentScope.WORKSPACE, canvasGrant, 7L, 1, 2L)).isFalse();
    }

    @Test
    void storageSerializationUsesWorkspaceFieldsAndNormalizesBack() {
        CanvasDocument doc = new CanvasDocument();
        doc.setCanvasId(15);
        doc.setCanvasName("Lab Notebook");
        doc.setAdminUserId(9L);
        doc.setVisibility("private");
        doc.setPeople(List.of(9L));
        doc.setItems(Map.of("cell-1", Map.of("id", "cell-1", "kind", "code")));

        ObjectNode canvasNode = CanvasDocumentJson.toStorageNode(objectMapper, doc, DocumentScope.CANVAS);
        assertThat(canvasNode.path("canvas-id").asInt()).isEqualTo(15);
        assertThat(canvasNode.has("workspace-id")).isFalse();

        ObjectNode workspaceNode = CanvasDocumentJson.toStorageNode(objectMapper, doc, DocumentScope.WORKSPACE);
        assertThat(workspaceNode.path("workspace-id").asInt()).isEqualTo(15);
        assertThat(workspaceNode.path("workspace-name").asText()).isEqualTo("Lab Notebook");
        assertThat(workspaceNode.has("canvas-id")).isFalse();
        assertThat(workspaceNode.has("canvas-name")).isFalse();

        ObjectNode normalized = CanvasDocumentJson.normalize(workspaceNode);
        assertThat(normalized.path("canvas-id").asInt()).isEqualTo(15);
        assertThat(normalized.path("canvas-name").asText()).isEqualTo("Lab Notebook");
    }

    @Test
    void elasticsearchWritesWorkspaceDocumentsToIsolatedWorkspaceIndex() {
        RestClient restClient = mock(RestClient.class);
        var putSpec = mock(RestClient.RequestBodyUriSpec.class);
        var responseSpec = mock(RestClient.ResponseSpec.class);
        when(restClient.put()).thenReturn(putSpec);
        when(putSpec.uri(eq("/{index}/_doc/{id}?refresh=true"), eq("workspace"), eq("21"))).thenReturn(putSpec);
        when(putSpec.contentType(any())).thenReturn(putSpec);
        when(putSpec.body(any(Object.class))).thenReturn(putSpec);
        when(putSpec.retrieve()).thenReturn(responseSpec);

        ElasticsearchProperties properties = new ElasticsearchProperties();
        properties.setHost("127.0.0.1");
        properties.setPort(9200);
        properties.setIndex("canvas");
        properties.setWorkspaceIndex("workspace");
        properties.setFailOnError(true);

        CanvasElasticsearchService service = new CanvasElasticsearchService(restClient, properties, objectMapper);
        CanvasDocument doc = new CanvasDocument();
        doc.setCanvasId(21);
        doc.setCanvasName("Theorem Proof");
        doc.setVisibility("private");
        doc.setPeople(List.of(5L));

        assertThat(service.saveDocument(DocumentScope.WORKSPACE, doc)).isTrue();
        verify(putSpec).uri("/{index}/_doc/{id}?refresh=true", "workspace", "21");
        verify(putSpec).body(argThat((Object raw) -> {
            String json = String.valueOf(raw);
            return json.contains("\"workspace-id\":21")
                    && json.contains("\"workspace-name\":\"Theorem Proof\"")
                    && !json.contains("\"canvas-id\"");
        }));
    }

    @Test
    void runtimeCleanupQueueTargetsSeparateTablesPerScope() {
        JdbcTemplate jdbc = mock(JdbcTemplate.class);
        RuntimeCleanupQueue queue = new RuntimeCleanupQueue(jdbc);

        queue.enqueue(DocumentScope.WORKSPACE, 77);
        queue.complete(DocumentScope.WORKSPACE, 77);

        verify(jdbc).update(
                argThat((String sql) -> sql.contains("dbo.workspace_runtime_cleanup") && sql.contains("workspace_id")),
                eq(77), any(), any());
        verify(jdbc).update("DELETE FROM dbo.workspace_runtime_cleanup WHERE workspace_id=?", 77);
    }

    @Test
    void loadBalancerSumsCachedCanvasesAndWorkspacesPerRedisInstance() {
        ServerInfoRepository serverRepo = mock(ServerInfoRepository.class);
        RedisInfoRepository redisRepo = mock(RedisInfoRepository.class);
        CanvasInfoRepository canvasRepo = mock(CanvasInfoRepository.class);
        WorkspaceInfoRepository workspaceRepo = mock(WorkspaceInfoRepository.class);
        CppServerClient cppClient = mock(CppServerClient.class);
        DatabaseProperties dbProps = mock(DatabaseProperties.class);

        when(canvasRepo.countByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue("10.0.0.2", "6379"))
                .thenReturn(4L);
        when(workspaceRepo.countByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue("10.0.0.2", "6379"))
                .thenReturn(3L);

        LoadBalancerService lb = new LoadBalancerService(
                serverRepo, redisRepo, canvasRepo, workspaceRepo, cppClient, dbProps);

        assertThat(lb.getRedisCanvasCountInJava("10.0.0.2", "6379")).isEqualTo(7);
    }
}
