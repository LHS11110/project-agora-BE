package com.endpoint.frelog.global.controller;

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

@Controller
public class TestPageController {

    private final HttpClient httpClient = HttpClient.newBuilder()
            .connectTimeout(Duration.ofSeconds(3))
            .build();

    @GetMapping(value = {"/test", "/test.jsp"})
    public String testPage(Model model) {
        model.addAttribute("pageTitle", "Agora Full System Real-Time Testbed");
        model.addAttribute("serverTime", LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")));
        model.addAttribute("activeEnv", "MSSQL + Elasticsearch + Redis + C++ Server");
        return "test";
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
        String host = (String) requestBody.getOrDefault("server_ip", "127.0.0.1");
        int port = requestBody.containsKey("server_port") ? Integer.parseInt(requestBody.get("server_port").toString()) : 8000;
        int canvasId = requestBody.containsKey("canvas_id") ? Integer.parseInt(requestBody.get("canvas_id").toString()) : 0;

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
     * Proxy endpoint for browser to get active canvas count on C++ server.
     */
    @GetMapping("/api/test/cpp-canvas-count")
    @ResponseBody
    public ResponseEntity<?> proxyCppCanvasCount(
            @RequestParam(defaultValue = "127.0.0.1") String host,
            @RequestParam(defaultValue = "8000") int port
    ) {
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
