package com.endpoint.frelog.domain.canvas.client;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import com.endpoint.frelog.global.logging.RequestCorrelationFilter;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import java.net.http.HttpClient;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManagerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;

import java.net.URI;
import java.time.Duration;
import java.util.UUID;

/** Reads live active-canvas load from a C++ realtime server. */
@Component
public class CppServerClient {

    private static final Logger log = LoggerFactory.getLogger(CppServerClient.class);
    private static final int UNAVAILABLE = Integer.MAX_VALUE;

    private final RestClient restClient;
    private final String internalApiToken;
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Autowired
    public CppServerClient(@Value("${app.cpp.internal-api-token:}") String internalApiToken,
                           @Value("${app.service-tls-ca}") String caPath) throws Exception {
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
        var httpClient = HttpClient.newBuilder().sslContext(sslContext).connectTimeout(Duration.ofSeconds(3)).build();
        var requestFactory = new JdkClientHttpRequestFactory(httpClient);
        requestFactory.setReadTimeout(Duration.ofMillis(3000));
        this.restClient = RestClient.builder().requestFactory(requestFactory).build();
        this.internalApiToken = internalApiToken;
    }

    public CppServerClient(RestClient restClient) {
        this.restClient = restClient;
        this.internalApiToken = "";
    }

    public int getCanvasCountFromServer(String serverIp, String serverPort) {
        try {
            String host = serverIp == null || serverIp.isBlank() ? "127.0.0.1" : serverIp.trim();
            String port = serverPort == null || serverPort.isBlank() ? "8000" : serverPort.trim();
            String requestId = MDC.get("request_id");
            if (requestId == null || requestId.isBlank()) requestId = UUID.randomUUID().toString();
            String body = restClient.get()
                    .uri(URI.create("https://" + host + ":" + port + "/api/canvas/count"))
                    .header("X-Agora-Internal-Token", internalApiToken)
                    .header(RequestCorrelationFilter.HEADER, requestId)
                    .retrieve()
                    .body(String.class);

            if (body == null) return UNAVAILABLE;
            JsonNode count = objectMapper.readTree(body).get("count");
            if (count == null || !count.canConvertToInt() || count.asInt() < 0) return UNAVAILABLE;
            return count.asInt();
        } catch (Exception e) {
            log.warn("C++ 서버({}:{}) 실시간 캔버스 부하 조회 실패: {}", serverIp, serverPort, e.getMessage());
            return UNAVAILABLE;
        }
    }

    public boolean isHealthy(String serverIp, String serverPort) {
        return getCanvasCountFromServer(serverIp, serverPort) != UNAVAILABLE;
    }
}
