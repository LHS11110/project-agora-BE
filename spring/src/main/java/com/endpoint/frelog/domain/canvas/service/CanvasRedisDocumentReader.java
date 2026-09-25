package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.logging.ElasticsearchBulkLogService;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Reads the active RedisJSON document; failure is fail-closed for access checks. */
@Component
public class CanvasRedisDocumentReader {
    private final ObjectMapper objectMapper;
    private final String username;
    private final String password;
    private final String sentinelAddresses;
    private final String sentinelMasterName;
    private final ElasticsearchBulkLogService logService;

    public CanvasRedisDocumentReader(ObjectMapper objectMapper,
            @Value("${REDIS_USER:}") String username,
            @Value("${REDIS_USER_PASSWORD:}") String password,
            @Value("${app.redis.sentinels:}") String sentinelAddresses,
            @Value("${app.redis.sentinel-master-name:agora-master}") String sentinelMasterName,
            ElasticsearchBulkLogService logService) {
        this.objectMapper = objectMapper;
        this.username = username;
        this.password = password;
        this.sentinelAddresses = sentinelAddresses;
        this.sentinelMasterName = sentinelMasterName;
        this.logService = logService;
    }

    public CanvasDocument read(CanvasInfo info) {
        if (info == null || password.isBlank()) throw unavailable();
        String key = "canvas:" + info.getCanvasId();

        if (sentinelAddresses.isBlank()) {
            if (info.getRedisInfo() == null) throw unavailable();
            try {
                RedisAddress endpoint = new RedisAddress(info.getRedisInfo().getRedisIp(),
                        Integer.parseInt(info.getRedisInfo().getRedisPort()));
                String json = readDocument(endpoint, false, key);
                logService.reportAvailability("redis", true, Map.of("endpoint", endpoint.host() + ":" + endpoint.port()));
                return objectMapper.readValue(json, CanvasDocument.class);
            } catch (Exception e) {
                logService.reportAvailability("redis", false,
                        Map.of("error_type", e.getClass().getSimpleName()));
                throw unavailable();
            }
        }

        List<RedisAddress> seeds = parseAddresses(sentinelAddresses);
        if (seeds.isEmpty()) throw unavailable();
        for (int attempt = 0; attempt < 3; attempt++) {
            for (RedisAddress seed : seeds) {
                RedisAddress master;
                String json;
                try {
                    master = discoverMaster(seed);
                    json = readDocument(master, true, key);
                } catch (Exception ignored) {
                    // A seed can briefly return its old view during promotion.
                    // Query the other seeds and verify the candidate's ROLE.
                    continue;
                }
                Map<String, String> details = Map.of("endpoint", master.host() + ":" + master.port());
                logService.reportAvailability("redis-sentinel", true, details);
                logService.reportPrimaryChange("redis-sentinel", master.host() + ":" + master.port());
                try {
                    return objectMapper.readValue(json, CanvasDocument.class);
                } catch (Exception e) {
                    throw unavailable();
                }
            }
            if (attempt < 2) {
                try {
                    Thread.sleep(100L * (attempt + 1));
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw unavailable();
                }
            }
        }
        logService.reportAvailability("redis-sentinel", false,
                Map.of("seed_count", seeds.size(), "error", "primary_discovery_or_document_read_failed"));
        throw unavailable();
    }

    private RedisAddress discoverMaster(RedisAddress sentinel) throws Exception {
        try (Socket socket = openSocket(sentinel)) {
            InputStream in = socket.getInputStream();
            OutputStream out = socket.getOutputStream();
            writeCommand(out, "SENTINEL", "get-master-addr-by-name", sentinelMasterName);
            Object reply = readReply(in);
            if (!(reply instanceof List<?> values) || values.size() < 2
                    || values.get(0) == null || values.get(1) == null) {
                throw new IOException("Sentinel did not return a master address");
            }
            String host = values.get(0).toString();
            int port = Integer.parseInt(values.get(1).toString());
            if (host.isBlank() || port < 1 || port > 65535) {
                throw new IOException("Sentinel returned an invalid master address");
            }
            return new RedisAddress(host, port);
        }
    }

    private String readDocument(RedisAddress endpoint, boolean requirePrimary, String key) throws Exception {
        try (Socket socket = openSocket(endpoint)) {
            InputStream in = socket.getInputStream();
            OutputStream out = socket.getOutputStream();
            if (username.isBlank()) writeCommand(out, "AUTH", password);
            else writeCommand(out, "AUTH", username, password);
            if (!"OK".equals(readReply(in))) throw new IOException("Redis authentication failed");

            if (requirePrimary) {
                writeCommand(out, "ROLE");
                Object role = readReply(in);
                if (!(role instanceof List<?> values) || values.isEmpty()
                        || !"master".equalsIgnoreCase(String.valueOf(values.get(0)))) {
                    throw new IOException("Sentinel endpoint is not the current primary");
                }
            }

            writeCommand(out, "JSON.GET", key);
            Object reply = readReply(in);
            if (!(reply instanceof String json) || json.isBlank()) {
                throw new IOException("Canvas document is missing");
            }
            return json;
        }
    }

    private Socket openSocket(RedisAddress address) throws IOException {
        Socket socket = new Socket();
        try {
            socket.connect(new InetSocketAddress(address.host(), address.port()), 1200);
            socket.setSoTimeout(2000);
            return socket;
        } catch (IOException e) {
            socket.close();
            throw e;
        }
    }

    private List<RedisAddress> parseAddresses(String configured) {
        List<RedisAddress> addresses = new ArrayList<>();
        for (String raw : configured.split(",")) {
            String entry = raw.trim();
            if (entry.isEmpty()) continue;

            String host;
            String port;
            if (entry.startsWith("[")) {
                int close = entry.indexOf(']');
                if (close < 0 || close + 1 >= entry.length() || entry.charAt(close + 1) != ':') continue;
                host = entry.substring(1, close);
                port = entry.substring(close + 2);
            } else {
                int separator = entry.lastIndexOf(':');
                if (separator < 1) continue;
                host = entry.substring(0, separator);
                port = entry.substring(separator + 1);
            }
            try {
                int parsedPort = Integer.parseInt(port);
                if (!host.isBlank() && parsedPort > 0 && parsedPort <= 65535) {
                    addresses.add(new RedisAddress(host, parsedPort));
                }
            } catch (NumberFormatException ignored) {
                // Ignore malformed seeds; if none remain, fail closed.
            }
        }
        return addresses;
    }

    private void writeCommand(OutputStream out, String... parts) throws IOException {
        out.write(("*" + parts.length + "\r\n").getBytes(StandardCharsets.UTF_8));
        for (String part : parts) {
            byte[] bytes = part.getBytes(StandardCharsets.UTF_8);
            out.write(("$" + bytes.length + "\r\n").getBytes(StandardCharsets.UTF_8));
            out.write(bytes);
            out.write("\r\n".getBytes(StandardCharsets.UTF_8));
        }
        out.flush();
    }

    private Object readReply(InputStream in) throws IOException {
        int kind = in.read();
        if (kind < 0) throw new IOException("Redis closed the connection");
        String line = readLine(in);
        return switch (kind) {
            case '+' -> line;
            case '-' -> throw new IOException("Redis command failed: " + line);
            case ':' -> Long.parseLong(line);
            case '$' -> readBulkString(in, Integer.parseInt(line));
            case '*' -> readArray(in, Integer.parseInt(line));
            default -> throw new IOException("Unsupported Redis response");
        };
    }

    private String readBulkString(InputStream in, int length) throws IOException {
        if (length < 0) return null;
        if (length > 16 * 1024 * 1024) throw new IOException("Redis response is too large");
        byte[] bytes = in.readNBytes(length);
        if (bytes.length != length || in.read() != '\r' || in.read() != '\n') {
            throw new IOException("Incomplete Redis response");
        }
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private List<Object> readArray(InputStream in, int count) throws IOException {
        if (count < 0) return null;
        if (count > 4096) throw new IOException("Redis response has too many elements");
        List<Object> values = new ArrayList<>(count);
        for (int i = 0; i < count; i++) values.add(readReply(in));
        return values;
    }

    private String readLine(InputStream in) throws IOException {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int value;
        while ((value = in.read()) >= 0 && buffer.size() < 1024) {
            if (value == '\r') {
                if (in.read() != '\n') throw new IOException("Malformed Redis response");
                return buffer.toString(StandardCharsets.UTF_8);
            }
            buffer.write(value);
        }
        throw new IOException("Incomplete Redis response line");
    }

    private CustomException unavailable() {
        return new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "활성 캔버스의 최신 상태를 확인할 수 없습니다. 잠시 후 다시 시도하세요.");
    }

    private record RedisAddress(String host, int port) {}
}
