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

/** Reads C++ load through Wall using only the registered server ID. */
@Component
public class CppServerClient {

    private static final Logger log = LoggerFactory.getLogger(CppServerClient.class);
    private static final int UNAVAILABLE = Integer.MAX_VALUE;

    private final RestClient restClient;
    private final String internalApiToken;
    private final URI wallOrigin;
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Autowired
    public CppServerClient(@Value("${app.cpp.internal-api-token:}") String internalApiToken,
                           @Value("${app.service-tls-ca}") String caPath,
                           @Value("${app.wall.cpp-api-origin}") String wallOrigin) throws Exception {
        this.wallOrigin = validateWallOrigin(wallOrigin);
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
        var httpClient = HttpClient.newBuilder().sslParameters(tlsParameters).sslContext(sslContext).connectTimeout(Duration.ofSeconds(3)).followRedirects(HttpClient.Redirect.NEVER).build();
        var requestFactory = new JdkClientHttpRequestFactory(httpClient);
        requestFactory.setReadTimeout(Duration.ofMillis(3000));
        this.restClient = RestClient.builder().requestFactory(requestFactory).build();
        this.internalApiToken = internalApiToken;
    }

    public CppServerClient(RestClient restClient, String wallOrigin, String internalApiToken) {
        this.restClient = restClient;
        this.wallOrigin = validateWallOrigin(wallOrigin);
        this.internalApiToken = internalApiToken;
    }

    private static URI validateWallOrigin(String value) {
        URI origin = URI.create(value);
        if (!"https".equals(origin.getScheme()) || origin.getHost() == null
                || origin.getUserInfo() != null || origin.getRawQuery() != null || origin.getRawFragment() != null
                || !(origin.getRawPath().isEmpty() || origin.getRawPath().equals("/"))) {
            throw new IllegalArgumentException("Wall C++ API origin must be an HTTPS origin");
        }
        return URI.create(value.replaceAll("/$", ""));
    }

    public int getCanvasCountFromServer(Integer serverId) {
        if (serverId == null || serverId <= 0) return UNAVAILABLE;
        try {
            String requestId = MDC.get("request_id");
            if (requestId == null || requestId.isBlank()) requestId = UUID.randomUUID().toString();
            String body = restClient.get()
                    .uri(wallOrigin.resolve("/internal/cpp/servers/" + serverId + "/api/canvas/count"))
                    .header("X-Agora-Internal-Token", internalApiToken)
                    .header(RequestCorrelationFilter.HEADER, requestId)
                    .retrieve()
                    .body(String.class);

            if (body == null) return UNAVAILABLE;
            JsonNode count = objectMapper.readTree(body).get("count");
            if (count == null || !count.canConvertToInt() || count.asInt() < 0) return UNAVAILABLE;
            return count.asInt();
        } catch (Exception e) {
            log.warn("Wall 경유 C++ 서버 #{} 실시간 캔버스 부하 조회 실패: {}", serverId, e.getMessage());
            return UNAVAILABLE;
        }
    }

    public boolean isHealthy(Integer serverId) {
        return getCanvasCountFromServer(serverId) != UNAVAILABLE;
    }
}
