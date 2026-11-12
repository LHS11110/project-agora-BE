package com.endpoint.frelog.domain.canvas.client;

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.logging.RequestCorrelationFilter;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.MDC;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;

import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManagerFactory;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.URI;
import java.net.http.HttpClient;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.time.Duration;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;

/** Sends authorized execution requests to the C++ broker assigned to the canvas. */
@Component
public class CppRuntimeClient {
    private static final int MAX_FRAME_BYTES = 400_000;
    private static final int MAX_STREAM_BYTES = 1_300_000;
    private final RestClient client;
    private final URI wallOrigin;
    private final String internalToken;
    private final ObjectMapper json = new ObjectMapper();

    public CppRuntimeClient(@Value("${app.cpp.internal-api-token:}") String internalToken,
                            @Value("${app.service-tls-ca}") String caPath,
                            @Value("${app.wall.cpp-api-origin}") String wallOrigin) throws Exception {
        this.wallOrigin = validateOrigin(wallOrigin);
        var trustStore = KeyStore.getInstance(KeyStore.getDefaultType());
        trustStore.load(null);
        try (var input = Files.newInputStream(Path.of(caPath))) {
            var certificates = CertificateFactory.getInstance("X.509").generateCertificates(input);
            int index = 0;
            for (var certificate : certificates) trustStore.setCertificateEntry("service-ca-" + index++, certificate);
        }
        var trustManagers = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        trustManagers.init(trustStore);
        var sslContext = SSLContext.getInstance("TLS");
        sslContext.init(null, trustManagers.getTrustManagers(), null);
        var tlsParameters = new javax.net.ssl.SSLParameters();
        tlsParameters.setProtocols(new String[]{"TLSv1.3"});
        var httpClient = HttpClient.newBuilder().sslParameters(tlsParameters).sslContext(sslContext)
                .connectTimeout(Duration.ofSeconds(3)).followRedirects(HttpClient.Redirect.NEVER).build();
        var requestFactory = new JdkClientHttpRequestFactory(httpClient);
        requestFactory.setReadTimeout(Duration.ofSeconds(240));
        this.client = RestClient.builder().requestFactory(requestFactory).build();
        this.internalToken = internalToken;
    }

    private static URI validateOrigin(String value) {
        URI origin = URI.create(value);
        if (!"https".equals(origin.getScheme()) || origin.getHost() == null || origin.getUserInfo() != null
                || origin.getRawQuery() != null || origin.getRawFragment() != null
                || !(origin.getRawPath().isEmpty() || origin.getRawPath().equals("/"))) {
            throw new IllegalArgumentException("Wall C++ API origin must be an HTTPS origin");
        }
        return URI.create(value.replaceAll("/$", ""));
    }

    public Map<String, Object> forward(Integer serverId, Map<String, Object> payload,
                                       Consumer<Map<String, Object>> onOutput) {
        if (serverId == null || serverId <= 0) throw new CustomException(ErrorCode.RUNTIME_CPP_SERVER_UNAVAILABLE);
        String requestId = MDC.get("request_id");
        if (requestId == null || requestId.isBlank()) requestId = UUID.randomUUID().toString();
        boolean stream = Boolean.TRUE.equals(payload.get("stream"));
        String path = "/internal/cpp/servers/" + serverId + "/api/runtime/forward" + (stream ? "/stream" : "");
        try {
            return client.post().uri(wallOrigin.resolve(path))
                    .header("X-Agora-Internal-Token", internalToken)
                    .header(RequestCorrelationFilter.HEADER, requestId)
                    .body(payload)
                    .exchange((request, response) -> {
                        int status = response.getStatusCode().value();
                        if (status != 200) {
                            String body = new String(response.getBody().readNBytes(MAX_FRAME_BYTES), StandardCharsets.UTF_8);
                            throw mapFailure(status, body);
                        }
                        if (!stream) {
                            Map<String,Object> result = json.readValue(response.getBody(), new TypeReference<Map<String, Object>>() {});
                            String runtimeError = String.valueOf(result.getOrDefault("error", ""));
                            if ("RUNTIME_CONFIG_INVALID".equals(runtimeError))
                                throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,
                                        String.valueOf(result.getOrDefault("message", "실행 환경 설정을 확인하세요.")));
                            if ("RUNTIME_UNAVAILABLE".equals(runtimeError))
                                throw new CustomException(ErrorCode.RUNTIME_VM_UNAVAILABLE);
                            return result;
                        }
                        if (onOutput == null) throw new IOException("Missing output receiver for a streamed request");
                        try (var reader = new BufferedReader(new InputStreamReader(response.getBody(), StandardCharsets.UTF_8))) {
                            String line;
                            int totalBytes = 0;
                            while ((line = reader.readLine()) != null) {
                                int frameBytes = line.getBytes(StandardCharsets.UTF_8).length;
                                totalBytes += frameBytes;
                                if (frameBytes > MAX_FRAME_BYTES || totalBytes > MAX_STREAM_BYTES)
                                    throw new IOException("C++ runtime response is too large");
                                Map<String, Object> frame = json.readValue(line, new TypeReference<Map<String, Object>>() {});
                                if ("output".equals(frame.get("type"))) {
                                    onOutput.accept(frame);
                                    continue;
                                }
                                if ("result".equals(frame.get("type")) && frame.get("result") instanceof Map<?, ?> result) {
                                    return json.convertValue(result, new TypeReference<Map<String, Object>>() {});
                                }
                                if ("error".equals(frame.get("type"))) {
                                    throw mapFailure(503, json.writeValueAsString(frame));
                                }
                                throw new IOException("Invalid C++ runtime frame");
                            }
                        }
                        throw new IOException("C++ runtime stream ended before its result frame");
                    });
        } catch (CustomException error) {
            throw error;
        } catch (Exception error) {
            throw new CustomException(ErrorCode.RUNTIME_CPP_SERVER_UNAVAILABLE,
                    "C++ 브로커가 VM 워커의 실행 결과를 반환하지 못했습니다.");
        }
    }

    private CustomException mapFailure(int status, String body) {
        try {
            Map<String, Object> response = json.readValue(body, new TypeReference<Map<String, Object>>() {});
            String code = String.valueOf(response.getOrDefault("error", response.getOrDefault("code", "")));
            String message = String.valueOf(response.getOrDefault("message", ""));
            if ("RUNTIME_EXECUTION_DENIED".equals(code) || "CANVAS_ACCESS_UNAUTHORIZED".equals(code))
                return new CustomException(ErrorCode.ACCESS_DENIED, message.isBlank() ? "브로커가 코드 실행 권한을 거부했습니다." : message);
            if ("RUNTIME_MANAGER_UNAVAILABLE".equals(code)) return new CustomException(ErrorCode.RUNTIME_MANAGER_UNAVAILABLE);
            if ("RUNTIME_VM_UNAVAILABLE".equals(code) || "RUNTIME_UNAVAILABLE".equals(code))
                return new CustomException(ErrorCode.RUNTIME_VM_UNAVAILABLE);
            if ("RUNTIME_CONFIG_INVALID".equals(code))
                return new CustomException(ErrorCode.INVALID_INPUT_VALUE, message.isBlank() ? "실행 환경 설정을 확인하세요." : message);
            if (status == 400) return new CustomException(ErrorCode.INVALID_INPUT_VALUE, message.isBlank() ? "실행 요청을 확인하세요." : message);
        } catch (Exception ignored) {
        }
        return new CustomException(ErrorCode.RUNTIME_CPP_SERVER_UNAVAILABLE);
    }
}
