package com.endpoint.frelog.global.controller;

import org.springframework.http.ResponseEntity;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.ResponseBody;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.HashMap;
import java.util.Map;

@Controller
public class TestPageController {

    @GetMapping(value = {"/test", "/test.jsp"})
    public String testPage(Model model) {
        model.addAttribute("pageTitle", "Agora Full System Real-Time Testbed");
        model.addAttribute("serverTime", LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")));
        model.addAttribute("activeEnv", "MSSQL + Elasticsearch + Redis + C++ Server");
        return "test";
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
