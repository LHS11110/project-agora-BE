package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

/** Reads the active RedisJSON document; failure is fail-closed for access checks. */
@Component
public class CanvasRedisDocumentReader {
    private final ObjectMapper objectMapper;
    private final String username;
    private final String password;

    public CanvasRedisDocumentReader(ObjectMapper objectMapper,
            @Value("${REDIS_USER:}") String username,
            @Value("${REDIS_USER_PASSWORD:}") String password) {
        this.objectMapper = objectMapper;
        this.username = username;
        this.password = password;
    }

    public CanvasDocument read(CanvasInfo info) {
        if (info.getRedisInfo() == null || password.isBlank()) throw unavailable();
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(info.getRedisInfo().getRedisIp(),
                    Integer.parseInt(info.getRedisInfo().getRedisPort())), 3000);
            socket.setSoTimeout(3000);
            InputStream in = socket.getInputStream();
            OutputStream out = socket.getOutputStream();
            if (username.isBlank()) writeCommand(out, "AUTH", password);
            else writeCommand(out, "AUTH", username, password);
            if (!"OK".equals(readReply(in))) throw unavailable();
            writeCommand(out, "JSON.GET", "canvas:" + info.getCanvasId());
            String json = readReply(in);
            if (json == null || json.isBlank()) throw unavailable();
            return objectMapper.readValue(json, CanvasDocument.class);
        } catch (Exception e) {
            throw unavailable();
        }
    }

    private void writeCommand(OutputStream out, String... parts) throws Exception {
        out.write(("*" + parts.length + "\r\n").getBytes(StandardCharsets.UTF_8));
        for (String part : parts) {
            byte[] bytes = part.getBytes(StandardCharsets.UTF_8);
            out.write(("$" + bytes.length + "\r\n").getBytes(StandardCharsets.UTF_8));
            out.write(bytes);
            out.write("\r\n".getBytes(StandardCharsets.UTF_8));
        }
        out.flush();
    }

    private String readReply(InputStream in) throws Exception {
        int kind = in.read();
        String line = readLine(in);
        if (kind == '+' || kind == ':') return line;
        if (kind == '$') {
            int length = Integer.parseInt(line);
            if (length < 0) return null;
            if (length > 16 * 1024 * 1024) throw unavailable();
            byte[] bytes = in.readNBytes(length);
            if (bytes.length != length || in.read() != '\r' || in.read() != '\n') throw unavailable();
            return new String(bytes, StandardCharsets.UTF_8);
        }
        throw unavailable();
    }

    private String readLine(InputStream in) throws Exception {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int value;
        while ((value = in.read()) >= 0 && buffer.size() < 128) {
            if (value == '\r') {
                if (in.read() != '\n') throw unavailable();
                return buffer.toString(StandardCharsets.UTF_8);
            }
            buffer.write(value);
        }
        throw unavailable();
    }

    private CustomException unavailable() {
        return new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "활성 캔버스의 최신 상태를 확인할 수 없습니다. 잠시 후 다시 시도하세요.");
    }
}
