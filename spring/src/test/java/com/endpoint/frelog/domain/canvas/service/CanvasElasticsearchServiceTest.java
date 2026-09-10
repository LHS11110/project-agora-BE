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

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
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
        // delete request throws 404
        var deleteSpec = mock(RestClient.RequestHeadersUriSpec.class);
        var deleteResponseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.delete()).willReturn(deleteSpec);
        given(deleteSpec.uri(eq("/{index}/_doc/{id}?refresh=true"), eq("canvas"), eq("Not Found"))).willReturn(deleteSpec);
        given(deleteSpec.retrieve()).willThrow(HttpClientErrorException.create(HttpStatusCode.valueOf(404), "Not Found", HttpHeaders.EMPTY, new byte[0], StandardCharsets.UTF_8));

        boolean result = service.deleteCanvas(100, "Not Found");
        assertThat(result).isFalse();
    }

    @Test
    @DisplayName("saveCanvas 시 인덱스가 없는 경우(404 index_not_found_exception) failOnError=false일 때 false 반환")
    void saveCanvas_IndexNotFound_FailOnErrorFalse() {
        var putSpec = mock(RestClient.RequestBodyUriSpec.class);
        given(restClient.put()).willReturn(putSpec);
        given(putSpec.uri(eq("/{index}/_doc/{id}?refresh=true"), eq("canvas"), eq("My Canvas"))).willReturn(putSpec);
        given(putSpec.contentType(any())).willReturn(putSpec);
        given(putSpec.body(any(Object.class))).willReturn(putSpec);

        String errorBody = "{\"error\":{\"root_cause\":[{\"type\":\"index_not_found_exception\",\"reason\":\"no such index [canvas]\"}],\"type\":\"index_not_found_exception\"},\"status\":404}";
        given(putSpec.retrieve()).willThrow(HttpClientErrorException.create(HttpStatusCode.valueOf(404), "Not Found", HttpHeaders.EMPTY, errorBody.getBytes(StandardCharsets.UTF_8), StandardCharsets.UTF_8));

        CanvasDocument doc = new CanvasDocument("My Canvas", 100, 1L, null, "default");
        boolean result = service.saveCanvas(doc);

        assertThat(result).isFalse();
    }

    @Test
    @DisplayName("saveCanvas 시 인덱스가 없는 경우 failOnError=true일 때 ELASTICSEARCH_INDEX_NOT_FOUND 예외 발생")
    void saveCanvas_IndexNotFound_FailOnErrorTrue() {
        properties.setFailOnError(true);

        var putSpec = mock(RestClient.RequestBodyUriSpec.class);
        given(restClient.put()).willReturn(putSpec);
        given(putSpec.uri(eq("/{index}/_doc/{id}?refresh=true"), eq("canvas"), eq("My Canvas"))).willReturn(putSpec);
        given(putSpec.contentType(any())).willReturn(putSpec);
        given(putSpec.body(any(Object.class))).willReturn(putSpec);

        String errorBody = "{\"error\":{\"root_cause\":[{\"type\":\"index_not_found_exception\",\"reason\":\"no such index [canvas]\"}],\"type\":\"index_not_found_exception\"},\"status\":404}";
        given(putSpec.retrieve()).willThrow(HttpClientErrorException.create(HttpStatusCode.valueOf(404), "Not Found", HttpHeaders.EMPTY, errorBody.getBytes(StandardCharsets.UTF_8), StandardCharsets.UTF_8));

        CanvasDocument doc = new CanvasDocument("My Canvas", 100, 1L, null, "default");

        assertThatThrownBy(() -> service.saveCanvas(doc))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.ELASTICSEARCH_INDEX_NOT_FOUND);
    }

    @Test
    @DisplayName("getCanvasDocumentById 시 인덱스 부재(404)인 경우 Optional.empty 반환")
    void getCanvasDocumentById_IndexNotFound() {
        var postSpec = mock(RestClient.RequestBodyUriSpec.class);
        given(restClient.post()).willReturn(postSpec);
        given(postSpec.uri(eq("/{index}/_search"), eq("canvas"))).willReturn(postSpec);
        given(postSpec.contentType(any())).willReturn(postSpec);
        given(postSpec.body(any(Object.class))).willReturn(postSpec);

        String errorBody = "{\"error\":{\"root_cause\":[{\"type\":\"index_not_found_exception\",\"reason\":\"no such index [canvas]\"}],\"type\":\"index_not_found_exception\"},\"status\":404}";
        given(postSpec.retrieve()).willThrow(HttpClientErrorException.create(HttpStatusCode.valueOf(404), "Not Found", HttpHeaders.EMPTY, errorBody.getBytes(StandardCharsets.UTF_8), StandardCharsets.UTF_8));

        Optional<CanvasDocument> doc = service.getCanvasDocumentById(100);
        assertThat(doc).isEmpty();
    }

    @Test
    @DisplayName("isIndexExists 정상 200 시 true 반환")
    void isIndexExists_True() {
        var headSpec = mock(RestClient.RequestHeadersUriSpec.class);
        var responseSpec = mock(RestClient.ResponseSpec.class);
        given(restClient.head()).willReturn(headSpec);
        given(headSpec.uri(eq("/{index}"), eq("canvas"))).willReturn(headSpec);
        given(headSpec.retrieve()).willReturn(responseSpec);

        boolean exists = service.isIndexExists();
        assertThat(exists).isTrue();
    }

    @Test
    @DisplayName("isIndexExists 404 시 false 반환")
    void isIndexExists_False() {
        var headSpec = mock(RestClient.RequestHeadersUriSpec.class);
        given(restClient.head()).willReturn(headSpec);
        given(headSpec.uri(eq("/{index}"), eq("canvas"))).willReturn(headSpec);
        given(headSpec.retrieve()).willThrow(HttpClientErrorException.create(HttpStatusCode.valueOf(404), "Not Found", HttpHeaders.EMPTY, new byte[0], StandardCharsets.UTF_8));

        boolean exists = service.isIndexExists();
        assertThat(exists).isFalse();
    }
}
