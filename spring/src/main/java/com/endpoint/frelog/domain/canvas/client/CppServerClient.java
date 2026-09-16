package com.endpoint.frelog.domain.canvas.client;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.MediaType;
import org.springframework.http.client.SimpleClientHttpRequestFactory;
import org.springframework.stereotype.Component;
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
        // Hairpin NAT 우회: 내부망 통신 시 공인 IP를 로컬 루프백으로 변환 (테스트 환경 단일 노드 기준)
        if (host.matches("^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}$") && !host.startsWith("10.") && !host.startsWith("172.") && !host.startsWith("192.168.") && !host.equals("127.0.0.1")) {
            log.info("Hairpin NAT 우회 적용: 공인 IP {} -> 127.0.0.1 로 변환하여 C++ 서버 호출", host);
            host = "127.0.0.1";
        }
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
    public boolean disconnectUser(String serverIp, String serverPort, Long userId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/users/" + userId + "/disconnect";
            restClient.post()
                    .uri(URI.create(url))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버({}:{}) 사용자 #{} 연결 해제 완료", serverIp, serverPort, userId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버({}:{}) 사용자 #{} 연결 해제 실패: {}", serverIp, serverPort, userId, e.getMessage());
            return false;
        }
    }

    /**
     * C++ 서버에서 특정 캔버스의 사용자 연결 즉시 종료 (POST /api/canvas/{canvasId}/users/{userId}/disconnect)
     */
    public boolean disconnectUserFromCanvas(String serverIp, String serverPort, Integer canvasId, Long userId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/users/" + userId + "/disconnect";
            restClient.post()
                    .uri(URI.create(url))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버({}:{}) 캔버스 #{} 사용자 #{} 연결 해제 완료", serverIp, serverPort, canvasId, userId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버({}:{}) 캔버스 #{} 사용자 #{} 연결 해제 실패, fallback to disconnectUser: {}", serverIp, serverPort, canvasId, userId, e.getMessage());
            return disconnectUser(serverIp, serverPort, userId);
        }
    }

    /**
     * 캔버스 이름 변경 즉시 반영 (POST /api/canvas/{canvasId}/reflect/name)
     */
    public boolean reflectCanvasName(String serverIp, String serverPort, Integer canvasId, String canvasName) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/name";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of("canvas_id", canvasId, "canvas_name", canvasName))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} 이름 즉시 반영 완료: {}", canvasId, canvasName);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} 이름 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 캔버스 소유자 변경 즉시 반영 (POST /api/canvas/{canvasId}/reflect/owner)
     */
    public boolean reflectCanvasOwner(String serverIp, String serverPort, Integer canvasId, Long oldOwnerId, Long newOwnerId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/owner";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of("canvas_id", canvasId, "old_owner_id", oldOwnerId, "new_owner_id", newOwnerId))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} 소유자 즉시 반영 완료 ({} -> {})", canvasId, oldOwnerId, newOwnerId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} 소유자 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 캔버스 설명 텍스트 변경 즉시 반영 (POST /api/canvas/{canvasId}/reflect/description)
     */
    public boolean reflectCanvasDescription(String serverIp, String serverPort, Integer canvasId, String description) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/description";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of("canvas_id", canvasId, "description", description != null ? description : ""))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} 설명 즉시 반영 완료", canvasId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} 설명 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 캔버스 비밀번호 변경 즉시 반영 (모든 사용자 재접속 요구) (POST /api/canvas/{canvasId}/reflect/password)
     */
    public boolean reflectCanvasPassword(String serverIp, String serverPort, Integer canvasId, String passwordHash) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/password";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of("canvas_id", canvasId, "password_hash", passwordHash != null ? passwordHash : ""))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} 비밀번호 즉시 반영 (재접속 요구) 완료", canvasId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} 비밀번호 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 초대된 사용자 리스트 변경 즉시 반영 (POST /api/canvas/{canvasId}/reflect/people)
     */
    public boolean reflectCanvasPeople(String serverIp, String serverPort, Integer canvasId, String action, Long userId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/people";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of("canvas_id", canvasId, "action", action, "user_id", userId))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} people 변경 즉시 반영 (action: {}, user_id: {}) 완료", canvasId, action, userId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} people 변경 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 내부 그룹 추가/제거 즉시 반영 (POST /api/canvas/{canvasId}/reflect/inner-group)
     */
    public boolean reflectInnerGroup(String serverIp, String serverPort, Integer canvasId, String action, String groupName, List<Long> affectedUsers) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/inner-group";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of(
                            "canvas_id", canvasId,
                            "action", action,
                            "group_name", groupName,
                            "affected_users", affectedUsers != null ? affectedUsers : List.of()
                    ))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} 내부 그룹 변경 즉시 반영 (action: {}, group: {}) 완료", canvasId, action, groupName);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} 내부 그룹 변경 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 그룹 멤버 추가/제거 즉시 반영 (POST /api/canvas/{canvasId}/reflect/group-member)
     */
    public boolean reflectGroupMember(String serverIp, String serverPort, Integer canvasId, String action, String groupName, Long userId) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/group-member";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of(
                            "canvas_id", canvasId,
                            "action", action,
                            "group_name", groupName,
                            "user_id", userId
                    ))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} 그룹 멤버 변경 즉시 반영 (action: {}, group: {}, user: {}) 완료", canvasId, action, groupName, userId);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} 그룹 멤버 변경 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 초기 그룹 변경 즉시 반영 (POST /api/canvas/{canvasId}/reflect/init-group)
     */
    public boolean reflectInitGroup(String serverIp, String serverPort, Integer canvasId, String oldGroup, String newGroup, List<Long> affectedUsers) {
        try {
            String url = getBaseUrl(serverIp, serverPort) + "/api/canvas/" + canvasId + "/reflect/init-group";
            restClient.post()
                    .uri(URI.create(url))
                    .contentType(MediaType.APPLICATION_JSON)
                    .body(Map.of(
                            "canvas_id", canvasId,
                            "old_group", oldGroup,
                            "new_group", newGroup,
                            "affected_users", affectedUsers != null ? affectedUsers : List.of()
                    ))
                    .retrieve()
                    .toBodilessEntity();
            log.info("C++ 서버 캔버스 #{} 초기 그룹 변경 즉시 반영 ({} -> {}) 완료", canvasId, oldGroup, newGroup);
            return true;
        } catch (Exception e) {
            log.warn("C++ 서버 캔버스 #{} 초기 그룹 변경 즉시 반영 실패: {}", canvasId, e.getMessage());
            return false;
        }
    }

    /**
     * 캔버스 삭제 시 C++ 서버 및 Redis 캐시 일괄 제거 (DELETE /api/canvas/{canvasId})
     */
    public boolean deleteCanvasFromServerAndRedis(String serverIp, String serverPort, Integer canvasId, String redisIp, String redisPort) {
        if (canvasId == null) {
            return false;
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
            return true;
        } catch (RestClientException | IllegalArgumentException e) {
            log.warn("C++ 서버({}:{}) 캔버스 #{} 삭제 API 호출 실패 (무시하고 DB/ES 삭제 계속 진행): {}",
                    serverIp, serverPort, canvasId, e.getMessage());
            return false;
        } catch (Exception e) {
            log.error("C++ 서버 통신 중 오류 발생: {}", e.getMessage(), e);
            return false;
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
