package com.endpoint.frelog.global.controller;

import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository;
import org.springframework.http.ResponseEntity;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.*;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;

@Controller
public class TestPageController {

    private static final Duration SERVER_HEARTBEAT_MAX_AGE = Duration.ofSeconds(15);
    private final ServerInfoRepository serverInfoRepository;

    private final HttpClient httpClient = HttpClient.newBuilder()
            .connectTimeout(Duration.ofSeconds(3))
            .build();

    public TestPageController(ServerInfoRepository serverInfoRepository) {
        this.serverInfoRepository = serverInfoRepository;
    }

    private boolean isFresh(ServerInfo server) {
        return Boolean.TRUE.equals(server.getIsActivated())
                && server.getLastHeartbeatAt() != null
                && server.getLastHeartbeatAt().isAfter(LocalDateTime.now().minus(SERVER_HEARTBEAT_MAX_AGE));
    }

    private Optional<ServerInfo> resolveRestServer(String host, int port) {
        return serverInfoRepository.findByServerIpAndServerPort(host, Integer.toString(port))
                .filter(this::isFresh);
    }

    private Optional<ServerInfo> resolveSocketServer(String host, int port) {
        String requestedPort = Integer.toString(port);
        return serverInfoRepository.findByIsActivatedTrueAndLastHeartbeatAtAfter(
                        LocalDateTime.now().minus(SERVER_HEARTBEAT_MAX_AGE))
                .stream()
                .filter(server -> server.getServerIp().equals(host)
                        && (server.getServerPort().equals(requestedPort) || server.getWsPort().equals(requestedPort)))
                .findFirst();
    }

    private ResponseEntity<?> invalidTarget() {
        return ResponseEntity.badRequest().body(Map.of("error", "등록되어 있고 정상 동작 중인 C++ 서버만 조회할 수 있습니다."));
    }


    /**
     * Proxy endpoint for browser to call C++ /api/access without CORS or cross-host network issues.
     */
    @PostMapping("/api/test/cpp-access")
    @ResponseBody
    public ResponseEntity<?> proxyCppAccess(
            @RequestBody Map<String, Object> requestBody,
            @RequestHeader(value = "Authorization", required = false) String authHeader
    ) {
        String host = String.valueOf(requestBody.getOrDefault("server_ip", "127.0.0.1"));
        int port = requestBody.containsKey("server_port") ? Integer.parseInt(requestBody.get("server_port").toString()) : 8000;
        int canvasId = requestBody.containsKey("canvas_id") ? Integer.parseInt(requestBody.get("canvas_id").toString()) : 0;

        ServerInfo target = resolveRestServer(host, port).orElse(null);
        if (target == null) return invalidTarget();
        host = target.getServerIp();
        port = Integer.parseInt(target.getServerPort());

        String cppUrl = "http://" + host + ":" + port + "/api/access";
        try {
            String jsonPayload = "{\"canvas_id\":" + canvasId + "}";
            HttpRequest.Builder reqBuilder = HttpRequest.newBuilder()
                    .uri(URI.create(cppUrl))
                    .timeout(Duration.ofSeconds(5))
                    .header("Content-Type", "application/json")
                    .POST(HttpRequest.BodyPublishers.ofString(jsonPayload));

            if (authHeader != null && !authHeader.isEmpty()) {
                reqBuilder.header("Authorization", authHeader);
            }

            HttpResponse<String> response = httpClient.send(reqBuilder.build(), HttpResponse.BodyHandlers.ofString());
            return ResponseEntity.status(response.statusCode())
                    .header("Content-Type", "application/json")
                    .body(response.body());
        } catch (Exception e) {
            Map<String, Object> error = new HashMap<>();
            error.put("error", "C++ 서버 통신 실패: " + e.getMessage());
            error.put("targetUrl", cppUrl);
            return ResponseEntity.status(502).body(error);
        }
    }

    /**
     * Proxy endpoint for browser to call C++ /api/access/disconnect directly.
     */
    @PostMapping("/api/test/cpp-disconnect")
    @ResponseBody
    public ResponseEntity<?> proxyCppDisconnect(
            @RequestBody Map<String, Object> requestBody,
            @RequestHeader(value = "Authorization", required = false) String authHeader
    ) {
        String host = String.valueOf(requestBody.getOrDefault("server_ip", "127.0.0.1"));
        int port = requestBody.containsKey("server_port") ? Integer.parseInt(requestBody.get("server_port").toString()) : 8000;
        int canvasId = requestBody.containsKey("canvas_id") ? Integer.parseInt(requestBody.get("canvas_id").toString()) : 0;
        int userId = requestBody.containsKey("user_id") ? Integer.parseInt(requestBody.get("user_id").toString()) : 0;

        ServerInfo target = resolveRestServer(host, port).orElse(null);
        if (target == null) return invalidTarget();
        host = target.getServerIp();
        port = Integer.parseInt(target.getServerPort());

        String cppUrl = "http://" + host + ":" + port + "/api/access/disconnect";
        try {
            String jsonPayload = "{\"canvas_id\":" + canvasId + ",\"user_id\":" + userId + "}";
            HttpRequest.Builder reqBuilder = HttpRequest.newBuilder()
                    .uri(URI.create(cppUrl))
                    .timeout(Duration.ofSeconds(5))
                    .header("Content-Type", "application/json")
                    .POST(HttpRequest.BodyPublishers.ofString(jsonPayload));

            if (authHeader != null && !authHeader.isEmpty()) {
                reqBuilder.header("Authorization", authHeader);
            }

            HttpResponse<String> response = httpClient.send(reqBuilder.build(), HttpResponse.BodyHandlers.ofString());
            return ResponseEntity.status(response.statusCode())
                    .header("Content-Type", "application/json")
                    .body(response.body());
        } catch (Exception e) {
            Map<String, Object> error = new HashMap<>();
            error.put("error", "C++ 서버 연결 종료 실패: " + e.getMessage());
            error.put("targetUrl", cppUrl);
            return ResponseEntity.status(502).body(error);
        }
    }

    /**
     * Proxy endpoint for browser to get active canvas count on C++ server.
     */
    @GetMapping("/api/test/cpp-canvas-count")
    @ResponseBody
    public ResponseEntity<?> proxyCppCanvasCount(
            @RequestParam(defaultValue = "127.0.0.1") String host,
            @RequestParam(defaultValue = "8000") int port
    ) {
        ServerInfo target = resolveRestServer(host, port).orElse(null);
        if (target == null) return invalidTarget();
        host = target.getServerIp();
        port = Integer.parseInt(target.getServerPort());
        String cppUrl = "http://" + host + ":" + port + "/api/canvas/count";
        try {
            HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(cppUrl))
                    .timeout(Duration.ofSeconds(3))
                    .GET()
                    .build();

            HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
            return ResponseEntity.status(response.statusCode())
                    .header("Content-Type", "application/json")
                    .body(response.body());
        } catch (Exception e) {
            Map<String, Object> error = new HashMap<>();
            error.put("error", "C++ 서버 통신 실패: " + e.getMessage());
            return ResponseEntity.status(502).body(error);
        }
    }

    /**
     * Proxy endpoint for browser to get active canvases detail list from C++ server.
     */
    @GetMapping("/api/test/cpp-active-canvases")
    @ResponseBody
    public ResponseEntity<?> proxyCppActiveCanvases(
            @RequestParam(defaultValue = "127.0.0.1") String host,
            @RequestParam(defaultValue = "8000") int port
    ) {
        ServerInfo target = resolveRestServer(host, port).orElse(null);
        if (target == null) return invalidTarget();
        host = target.getServerIp();
        port = Integer.parseInt(target.getServerPort());
        String cppUrl = "http://" + host + ":" + port + "/api/canvas/active";
        try {
            HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(cppUrl))
                    .timeout(Duration.ofSeconds(3))
                    .GET()
                    .build();

            HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
            return ResponseEntity.status(response.statusCode())
                    .header("Content-Type", "application/json")
                    .body(response.body());
        } catch (Exception e) {
            Map<String, Object> error = new HashMap<>();
            error.put("error", "C++ 서버 통신 실패: " + e.getMessage());
            return ResponseEntity.status(502).body(error);
        }
    }

    /**
     * Helper endpoint for JSP browser to test raw TCP connection to C++ allocated RX/TX sockets.
     */
    @GetMapping("/api/test/socket-ping")
    @ResponseBody
    public ResponseEntity<?> testSocketConnection(
            @RequestParam(defaultValue = "127.0.0.1") String host,
            @RequestParam int port,
            @RequestParam(defaultValue = "3000") int timeoutMs
    ) {
        ServerInfo target = resolveSocketServer(host, port).orElse(null);
        if (target == null) return invalidTarget();
        host = target.getServerIp();
        timeoutMs = Math.max(100, Math.min(timeoutMs, 5000));
        Map<String, Object> result = new HashMap<>();
        result.put("host", host);
        result.put("port", port);

        long start = System.currentTimeMillis();
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(host, port), timeoutMs);
            socket.setSoTimeout(timeoutMs);

            result.put("connected", true);
            result.put("latencyMs", System.currentTimeMillis() - start);

            // Read initial message if available (e.g. RX port sends init_items JSON)
            try {
                BufferedReader reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
                String line = reader.readLine();
                result.put("receivedMessage", line != null ? line : "(no initial message, socket ready)");
            } catch (Exception e) {
                result.put("receivedMessage", "(socket open, ready for communication)");
            }

            return ResponseEntity.ok(result);
        } catch (Exception e) {
            result.put("connected", false);
            result.put("error", e.getClass().getSimpleName() + ": " + e.getMessage());
            result.put("latencyMs", System.currentTimeMillis() - start);
            return ResponseEntity.badRequest().body(result);
        }
    }
}
