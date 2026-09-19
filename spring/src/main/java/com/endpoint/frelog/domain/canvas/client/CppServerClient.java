package com.endpoint.frelog.domain.canvas.client;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.MediaType;
import org.springframework.http.client.SimpleClientHttpRequestFactory;
import org.springframework.stereotype.Component;
import org.springframework.scheduling.annotation.Async;
import org.springframework.web.client.RestClient;
import org.springframework.web.client.RestClientException;
import org.springframework.web.util.UriComponentsBuilder;

import java.net.URI;
import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * C++ 실시간 통신 서버와의 HTTP 통신을 전담하는 클라이언트 컴포넌트
 */
@Component
public class CppServerClient {

    private static final Logger log = LoggerFactory.getLogger(CppServerClient.class);

    private final RestClient restClient;

    public CppServerClient() {
        SimpleClientHttpRequestFactory requestFactory = new SimpleClientHttpRequestFactory();
        requestFactory.setConnectTimeout(Duration.ofMillis(3000));
        requestFactory.setReadTimeout(Duration.ofMillis(3000));

        this.restClient = RestClient.builder()
                .requestFactory(requestFactory)
                .build();
    }

    public CppServerClient(RestClient restClient) {
        this.restClient = restClient;
    }

    private String getBaseUrl(String serverIp, String serverPort) {
        String host = (serverIp != null && !serverIp.isBlank()) ? serverIp.trim() : "127.0.0.1";

        String port = (serverPort != null && !serverPort.isBlank()) ? serverPort.trim() : "8000";
        return "http://" + host + ":" + port;
    }

    /**
     * C++ 서버에 JWT 토큰 등록 및 캔버스 활성화 (POST /api/auth/token)
     */
    public boolean registerJwtToken(String serverIp, String serverPort, Long userId, String token) {
        return registerJwtToken(serverIp, serverPort, userId, token, null);
    }

    public boolean registerJwtToken(String serverIp, String serverPort, Long userId, String token, Integer canvasId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/auth/token";
            Map<String, Object> body = new HashMap<>();
            body.put("user_id", userId);
            body.put("token", token);
            if (canvasId != null) {
                body.put("canvas_id", canvasId);
            }
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(body)
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버({}:{}) 사용자 #{} JWT 토큰 등록 및 캔버스 #{} 활성화 완료", serverIp, serverPort, userId, canvasId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버({}:{}) 사용자 #{} JWT 토큰 등록 실패: {}", serverIp, serverPort, userId, e.getMessage());
            return false;
        }
    }

    /**
     * C++ 서버에서 사용자 연결 즉시 종료 (POST /api/users/{userId}/disconnect)
     */
    @Async
    public void disconnectUser(String serverIp, String serverPort, Long userId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/users/" + userId + "/disconnect";
            restClient.post()
                    .uri(URI.create(url))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버({}:{}) 사용자 #{} 연결 해제 완료", serverIp, serverPort, userId);
            } catch (Exception e) {
            log.warn("C++ 서버({}:{}) 사용자 #{} 연결 해제 실패: {}", serverIp, serverPort, userId, e.getMessage());
            }
    }

    /**
     * C++ 서버에서 특정 캔버스의 사용자 연결 즉시 종료 (POST /api/canvas/{canvasId}/users/{userId}/disconnect)
     */
    @Async
    public void disconnectUserFromCanvas(String serverIp, String serverPort, Integer canvasId, Long userId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/users/" + userId + "/disconnect";
            restClient.post()
                    .uri(URI.create(url))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버({}:{}) 캔버스 #{} 사용자 #{} 연결 해제 완료", serverIp, serverPort, canvasId, userId);
            } catch (Exception e) {
            log.warn("C++ 서버({}:{}) 캔버스 #{} 사용자 #{} 연결 해제 실패, fallback to disconnectUser: {}", serverIp, serverPort, canvasId, userId, e.getMessage());
            disconnectUser(serverIp, serverPort, userId);}
    }



    /**
     * 캔버스 삭제 시 C++ 서버 및 Redis 캐시 일괄 제거 (DELETE /api/canvas/{canvasId})
     */
    @Async
    public void deleteCanvasFromServerAndRedis(String serverIp, String serverPort, Integer canvasId, String redisIp, String redisPort) {
        if (canvasId == null) {
            }

        try {
            UriComponentsBuilder uriBuilder = UriComponentsBuilder.fromUriString(getBaseUrl(serverIp, serverPort))
                    .path("/api/canvas/{canvasId}");

            if (redisIp != null && !redisIp.isBlank()) {
                uriBuilder.queryParam("redisIp", redisIp.trim());
            }
            if (redisPort != null && !redisPort.isBlank()) {
                uriBuilder.queryParam("redisPort", redisPort.trim());
            }

            URI targetUri = uriBuilder.buildAndExpand(canvasId).toUri();
            log.info("C++ 서버 캔버스 삭제 요청 전송: {}", targetUri);

            restClient.delete()
                    .uri(targetUri)
                    .retrieve()
                    .toBodilessEntity();

            log.info("C++ 서버 캔버스 #{} 메모리 및 Redis 캐시 일괄 삭제 완료", canvasId);
            } catch (RestClientException | IllegalArgumentException e) {
            log.warn("C++ 서버({}:{}) 캔버스 #{} 삭제 API 호출 실패 (무시하고 DB/ES 삭제 계속 진행): {}",
                    serverIp, serverPort, canvasId, e.getMessage());
            } catch (Exception e) {
            log.error("C++ 서버 통신 중 오류 발생: {}", e.getMessage(), e);
            }
    }

    /**
     * C++ 서버 활성 캔버스 수 조회 (GET /api/canvas/count)
     */
    public int getCanvasCountFromServer(String serverIp, String serverPort) {
        try {
            URI targetUri = URI.create(getBaseUrl(serverIp, serverPort) + "/api/canvas/count");
            String rawJson = restClient.get()
                    .uri(targetUri)
                    .retrieve()
                    .body(String.class);

            if (rawJson != null) {
                com.fasterxml.jackson.databind.JsonNode root = new com.fasterxml.jackson.databind.ObjectMapper().readTree(rawJson);
                if (root.has("count")) {
                    return root.get("count").asInt();
                }
            }
            return 0;
        } catch (Exception e) {
            log.warn("C++ 서버({}:{}) 캔버스 개수 조회 실패: {}", serverIp, serverPort, e.getMessage());
            return Integer.MAX_VALUE;
        }
    }
}
