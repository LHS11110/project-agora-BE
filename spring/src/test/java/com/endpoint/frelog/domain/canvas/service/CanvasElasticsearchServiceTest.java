package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasDocumentRequest;
import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatusCode;
import org.springframework.web.client.HttpClientErrorException;
import org.springframework.web.client.RestClient;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

@ExtendWith(MockitoExtension.class)
class CanvasElasticsearchServiceTest {

    @Mock
    private RestClient restClient;

    private ElasticsearchProperties properties;
    private final ObjectMapper objectMapper = new ObjectMapper();
    private CanvasElasticsearchService service;

    @BeforeEach
    void setUp() {
        properties = new ElasticsearchProperties();
        properties.setHost("127.0.0.1");
        properties.setPort(9200);
        properties.setIndex("canvas");
        properties.setFailOnError(false);

        service = new CanvasElasticsearchService(restClient, properties, objectMapper);
    }

    @Test
    @DisplayName("isAvailable 정상 응답 시 true 반환")
    void isAvailable_Success() {
        var spec = mock(RestClient.RequestHeadersUriSpec.class);
        var responseSpec = mock(RestClient.ResponseSpec.class);

        given(restClient.get()).willReturn(spec);
        given(spec.uri("/")).willReturn(spec);
        given(spec.retrieve()).willReturn(responseSpec);

        boolean available = service.isAvailable();
        assertThat(available).isTrue();
    }

    @Test
    @DisplayName("isAvailable 에러 발생 시 false 반환")
    void isAvailable_Failure() {
        given(restClient.get()).willThrow(new RuntimeException("Connection refused"));

        boolean available = service.isAvailable();
        assertThat(available).isFalse();
    }

    @Test
    @DisplayName("saveCanvas 성공 시 true 반환 및 PUT 요청 수행")
    void saveCanvas_Success() {
        // ensureIndex get
        var getSpec = mock(RestClient.RequestHeadersUriSpec.class);
        var getResponseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.get()).willReturn(getSpec);
        given(getSpec.uri("/{index}", "canvas")).willReturn(getSpec);
        given(getSpec.retrieve()).willReturn(getResponseSpec);

        // put request
        var putSpec = mock(RestClient.RequestBodyUriSpec.class);
        var putResponseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.put()).willReturn(putSpec);
        given(putSpec.uri(eq("/{index}/_doc/{id}?refresh=true"), eq("canvas"), eq("My Canvas"))).willReturn(putSpec);
        given(putSpec.contentType(any())).willReturn(putSpec);
        given(putSpec.body(any(Object.class))).willReturn(putSpec);
        given(putSpec.retrieve()).willReturn(putResponseSpec);

        CanvasDocument doc = new CanvasDocument("My Canvas", 100, 1L, null, "default");
        boolean result = service.saveCanvas(doc);

        assertThat(result).isTrue();
    }

    @Test
    @DisplayName("deleteCanvas 성공 시 true 반환 및 DELETE 요청 수행")
    void deleteCanvas_Success() {
        // ensureIndex get
        var getSpec = mock(RestClient.RequestHeadersUriSpec.class);
        var getResponseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.get()).willReturn(getSpec);
        given(getSpec.uri("/{index}", "canvas")).willReturn(getSpec);
        given(getSpec.retrieve()).willReturn(getResponseSpec);

        // delete request
        var deleteSpec = mock(RestClient.RequestHeadersUriSpec.class);
        var deleteResponseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.delete()).willReturn(deleteSpec);
        given(deleteSpec.uri(eq("/{index}/_doc/{id}?refresh=true"), eq("canvas"), eq("Delete Me"))).willReturn(deleteSpec);
        given(deleteSpec.retrieve()).willReturn(deleteResponseSpec);

        boolean result = service.deleteCanvas(100, "Delete Me");
        assertThat(result).isTrue();
    }

    @Test
    @DisplayName("deleteCanvas 대상이 404인 경우 false 반환")
    void deleteCanvas_NotFound() {
        // ensureIndex get
        var getSpec = mock(RestClient.RequestHeadersUriSpec.class);
        var getResponseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.get()).willReturn(getSpec);
        given(getSpec.uri("/{index}", "canvas")).willReturn(getSpec);
        given(getSpec.retrieve()).willReturn(getResponseSpec);

        // delete request throws 404
        var deleteSpec = mock(RestClient.RequestHeadersUriSpec.class);
        var deleteResponseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.delete()).willReturn(deleteSpec);
        given(deleteSpec.uri(eq("/{index}/_doc/{id}?refresh=true"), eq("canvas"), eq("Not Found"))).willReturn(deleteSpec);
        given(deleteSpec.retrieve()).willThrow(HttpClientErrorException.create(HttpStatusCode.valueOf(404), "Not Found", HttpHeaders.EMPTY, new byte[0], StandardCharsets.UTF_8));

        boolean result = service.deleteCanvas(100, "Not Found");
        assertThat(result).isFalse();
    }
}
