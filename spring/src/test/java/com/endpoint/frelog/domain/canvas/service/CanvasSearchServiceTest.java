package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.springframework.web.client.RestClient;
import java.util.List;
import java.util.Map;
import java.util.ArrayList;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.mockito.ArgumentMatchers.*;

class CanvasSearchServiceTest {
    private final ObjectMapper mapper = new ObjectMapper();
    @Test void rankFusionPromotesAgreementAndDeduplicates() {
        assertThat(CanvasSearchService.fuse(List.of("1", "2"), List.of("2", "3"), List.of("2", "4"), 3))
            .containsExactly("2", "1", "3");
    }
    @Test void knnNeverMixesModelRevisions() {
        var body = CanvasSearchService.vectorQuery(new CanvasEmbeddingClient.Embedding(List.of(1.0), "revision-A"), 10, 100, .8);
        assertThat(mapper.valueToTree(body).at("/knn/filter/term/model-revision").asText()).isEqualTo("revision-A");
        assertThat(mapper.valueToTree(body).at("/knn/similarity").asDouble()).isEqualTo(.8);
    }
    @Test void embeddingFailurePreservesKeywordsAndFiltersDeletedCanonicalDocuments() throws Exception {
        RestClient es = mock(RestClient.class);
        var spec = mock(RestClient.RequestBodyUriSpec.class);
        var response = mock(RestClient.ResponseSpec.class);
        when(es.post()).thenReturn(spec);
        when(spec.uri(eq("/{index}/{operation}"), any(), any())).thenReturn(spec);
        when(spec.contentType(any())).thenReturn(spec);
        List<String> requests = new ArrayList<>();
        when(spec.body(any(Object.class))).thenAnswer(invocation -> { requests.add((String)invocation.getArgument(0)); return spec; });
        when(spec.retrieve()).thenReturn(response);
        when(response.body(String.class)).thenReturn(
            "{\"hits\":{\"hits\":[{\"_id\":\"1\"},{\"_id\":\"2\"}]}}",
            "{\"hits\":{\"hits\":[{\"_id\":\"2\"}]}}",
            "{\"docs\":[{\"_id\":\"1\",\"found\":false},{\"_id\":\"2\",\"found\":true,\"_source\":{\"canvas-id\":2,\"canvas-name\":\"현재 이름\",\"people\":[1,2]}}]}");
        CanvasEmbeddingClient embeddings = mock(CanvasEmbeddingClient.class);
        when(embeddings.embed("AI")).thenThrow(new IllegalStateException("Unavailable"));
        var service = new CanvasSearchService(es, new ElasticsearchProperties(), mapper, embeddings);
        var documents = service.search("AI");
        assertThat(documents).hasSize(1);
        assertThat(documents.getFirst().getCanvasName()).isEqualTo("현재 이름");
        assertThat(documents.getFirst().getPeople()).hasSize(2);
        var fields = mapper.readTree(requests.getLast()).at("/docs/0/_source");
        assertThat(fields.toString()).doesNotContain("items", "password", "inner-group");
        assertThat(mapper.readTree(requests.getFirst()).path("_source").asBoolean(true)).isFalse();
        verify(es, times(3)).post();
    }
    @Test void projectionFailureStillUsesCanonicalSearch() throws Exception {
        RestClient es = mock(RestClient.class); var spec = mock(RestClient.RequestBodyUriSpec.class);
        var response = mock(RestClient.ResponseSpec.class);
        when(es.post()).thenThrow(new IllegalStateException("Projection absent")).thenReturn(spec);
        when(spec.uri(eq("/{index}/{operation}"), any(), any())).thenReturn(spec);
        when(spec.contentType(any())).thenReturn(spec); when(spec.body(any(Object.class))).thenReturn(spec);
        when(spec.retrieve()).thenReturn(response); when(response.body(String.class)).thenReturn("{\"hits\":{\"hits\":[]}}");
        var properties = new ElasticsearchProperties(); properties.setSemanticSearchEnabled(false);
        assertThat(new CanvasSearchService(es, properties, mapper, null).search("query")).isEmpty();
    }
    @Test void rejectsUnboundedQueriesAndNonTlsEmbeddingEndpoint() {
        var search = new CanvasSearchService(mock(RestClient.class), new ElasticsearchProperties(), mapper, null);
        assertThatThrownBy(() -> search.search("x".repeat(513))).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> new CanvasEmbeddingClient("http://localhost/embed", "", "", mapper))
            .isInstanceOf(IllegalArgumentException.class);
    }
}
