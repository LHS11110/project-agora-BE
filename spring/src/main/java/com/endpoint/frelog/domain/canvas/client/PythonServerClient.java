package com.endpoint.frelog.domain.canvas.client;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.client.SimpleClientHttpRequestFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;
import org.springframework.web.client.RestClientException;
import org.springframework.web.util.UriComponentsBuilder;

import java.net.URI;
import java.time.Duration;

/**
 * Python 서버(FastAPI)와의 HTTP 통신을 전담하는 클라이언트 컴포넌트
 * - 캔버스 삭제 시 Python 서버 메모리 및 Redis 캐시 일괄 제거 요청
 */
@Component
public class PythonServerClient {

    private static final Logger log = LoggerFactory.getLogger(PythonServerClient.class);

    private final RestClient restClient;

    public PythonServerClient() {
        SimpleClientHttpRequestFactory requestFactory = new SimpleClientHttpRequestFactory();
        requestFactory.setConnectTimeout(Duration.ofMillis(3000));
        requestFactory.setReadTimeout(Duration.ofMillis(3000));

        this.restClient = RestClient.builder()
                .requestFactory(requestFactory)
                .build();
    }

    public PythonServerClient(RestClient restClient) {
        this.restClient = restClient;
    }

    /**
     * Python 서버에 캔버스 및 Redis 캐시 일괄 삭제(DELETE /api/canvas/{canvasId}) 요청 전송
     *
     * @param serverIp   Python 서버 IP
     * @param serverPort Python 서버 Port
     * @param canvasId   캔버스 고유 ID
     * @param redisIp    할당된 Redis IP (optional)
     * @param redisPort  할당된 Redis Port (optional)
     * @return 성공 여부 (장애 격리를 위해 통신 실패 시에도 false를 반환하고 예외 미전파)
     */
    public boolean deleteCanvasFromServerAndRedis(String serverIp, String serverPort, Integer canvasId, String redisIp, String redisPort) {
        if (canvasId == null) {
            return false;
        }

        String host = (serverIp != null && !serverIp.isBlank()) ? serverIp.trim() : "127.0.0.1";
        String port = (serverPort != null && !serverPort.isBlank()) ? serverPort.trim() : "8000";

        try {
            UriComponentsBuilder uriBuilder = UriComponentsBuilder.newInstance()
                    .scheme("http")
                    .host(host)
                    .port(Integer.parseInt(port))
                    .path("/api/canvas/{canvasId}");

            if (redisIp != null && !redisIp.isBlank()) {
                uriBuilder.queryParam("redisIp", redisIp.trim());
            }
            if (redisPort != null && !redisPort.isBlank()) {
                uriBuilder.queryParam("redisPort", redisPort.trim());
            }

            URI targetUri = uriBuilder.buildAndExpand(canvasId).toUri();
            log.info("Python 서버 캔버스/Redis 일괄 삭제 요청 전송: {}", targetUri);

            restClient.delete()
                    .uri(targetUri)
                    .retrieve()
                    .toBodilessEntity();

            log.info("Python 서버 캔버스 #{} 메모리 및 Redis 캐시 일괄 삭제 완료 ({}:{})", canvasId, host, port);
            return true;
        } catch (RestClientException | IllegalArgumentException e) {
            log.warn("Python 서버({}:{}) 캔버스 #{} 삭제 API 호출 실패 (무시하고 DB/ES 삭제 계속 진행): {}",
                    host, port, canvasId, e.getMessage());
            return false;
        } catch (Exception e) {
            log.error("Python 서버 통신 중 예상치 못한 오류 발생: {}", e.getMessage(), e);
            return false;
        }
    }
}
