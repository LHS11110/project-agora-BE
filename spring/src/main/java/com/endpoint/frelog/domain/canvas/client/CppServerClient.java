package com.endpoint.frelog.domain.canvas.client;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.client.SimpleClientHttpRequestFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;

import java.net.URI;
import java.time.Duration;

/** Reads live active-canvas load from a C++ realtime server. */
@Component
public class CppServerClient {

    private static final Logger log = LoggerFactory.getLogger(CppServerClient.class);
    private static final int UNAVAILABLE = Integer.MAX_VALUE;

    private final RestClient restClient;
    private final ObjectMapper objectMapper = new ObjectMapper();

    public CppServerClient() {
        SimpleClientHttpRequestFactory requestFactory = new SimpleClientHttpRequestFactory();
        requestFactory.setConnectTimeout(Duration.ofMillis(3000));
        requestFactory.setReadTimeout(Duration.ofMillis(3000));
        this.restClient = RestClient.builder().requestFactory(requestFactory).build();
    }

    public CppServerClient(RestClient restClient) {
        this.restClient = restClient;
    }

    public int getCanvasCountFromServer(String serverIp, String serverPort) {
        try {
            String host = serverIp == null || serverIp.isBlank() ? "127.0.0.1" : serverIp.trim();
            String port = serverPort == null || serverPort.isBlank() ? "8000" : serverPort.trim();
            String body = restClient.get()
                    .uri(URI.create("http://" + host + ":" + port + "/api/canvas/count"))
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
