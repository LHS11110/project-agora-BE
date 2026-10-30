package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Set;

@Service
public class CanvasRuntimeService {
    private static final Set<String> LANGUAGES = Set.of("python", "javascript", "c", "cpp", "bash", "java");
    private static final int MAX_CODE_BYTES = 65536;
    private static final int MAX_RESPONSE_BYTES = 400000;
    private final ObjectMapper json = new ObjectMapper();
    private final String socketPath;

    public CanvasRuntimeService(@Value("${AGORA_RUNTIME_SOCKET:/run/agora-runtime/manager.sock}") String socketPath) {
        this.socketPath = socketPath;
    }

    public Map<String, Object> execute(Integer canvasId, String language, String code) {
        if (language == null || !LANGUAGES.contains(language) || code == null ||
                code.getBytes(StandardCharsets.UTF_8).length > MAX_CODE_BYTES) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "지원하는 언어와 64KB 이하의 코드를 입력하세요.");
        }
        return exchange(Map.of("canvasId", canvasId, "language", language, "code", code));
    }

    public Map<String, Object> settings(Integer canvasId) {
        return exchange(Map.of("canvasId", canvasId, "action", "settings_get"));
    }

    public Map<String, Object> configure(Integer canvasId, String language, Map<String, Object> config, Long revision) {
        if (language == null || !LANGUAGES.contains(language) || config == null || config.size() > 12 || revision == null || revision < 0) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "실행 언어와 옵션, 설정 버전을 확인하세요.");
        }
        return exchange(Map.of("canvasId", canvasId, "action", "settings_set", "language", language, "config", config, "revision", revision));
    }

    public Map<String, Object> resources(Integer canvasId, String template, Long revision) {
        if (template == null || !Set.of("small", "medium", "large").contains(template) || revision == null || revision < 0) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "VM 리소스 템플릿과 설정 버전을 확인하세요.");
        }
        return exchange(Map.of("canvasId", canvasId, "action", "resources_set", "template", template, "revision", revision));
    }

    public Map<String, Object> disk(Integer canvasId, Integer sizeGiB, Long revision) {
        if (sizeGiB == null || !Set.of(6, 12, 24).contains(sizeGiB) || revision == null || revision < 0) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "디스크 용량과 설정 버전을 확인하세요.");
        }
        return exchange(Map.of("canvasId", canvasId, "action", "disk_set", "sizeGiB", sizeGiB, "revision", revision));
    }

    public Map<String, Object> installJavaDependencies(Integer canvasId) {
        return exchange(Map.of("canvasId",canvasId,"action","java_dependencies"));
    }

    public void delete(Integer canvasId) {
        if (!Boolean.TRUE.equals(exchange(Map.of("canvasId",canvasId,"action","delete")).get("deleted"))) {
            throw new CustomException(ErrorCode.NO_SERVER_AVAILABLE, "실행 환경 삭제를 완료하지 못했습니다.");
        }
    }

    public Map<String, Object> reset(Integer canvasId) {
        return exchange(Map.of("canvasId", canvasId, "action", "reset"));
    }

    private Map<String, Object> exchange(Map<String, Object> payload) {
        try (SocketChannel channel = SocketChannel.open(StandardProtocolFamily.UNIX);
             Selector selector = Selector.open()) {
            channel.configureBlocking(false);
            channel.connect(UnixDomainSocketAddress.of(socketPath));
            long deadline = System.nanoTime() + ("java_dependencies".equals(payload.get("action")) ? 220_000_000_000L : 55_000_000_000L);
            channel.register(selector, SelectionKey.OP_CONNECT);
            while (!channel.finishConnect()) waitFor(selector, deadline);
            byte[] request = (json.writeValueAsString(payload) + "\n")
                    .getBytes(StandardCharsets.UTF_8);
            ByteBuffer output = ByteBuffer.wrap(request);
            channel.keyFor(selector).interestOps(SelectionKey.OP_WRITE);
            while (output.hasRemaining()) {
                if (channel.write(output) == 0) waitFor(selector, deadline);
            }
            channel.keyFor(selector).interestOps(SelectionKey.OP_READ);
            var response = new ByteArrayOutputStream();
            ByteBuffer input = ByteBuffer.allocate(8192);
            while (true) {
                int count = channel.read(input);
                if (count < 0) throw new IOException("Runtime disconnected");
                if (count == 0) { waitFor(selector, deadline); continue; }
                input.flip();
                while (input.hasRemaining()) {
                    byte b = input.get();
                    if (b == '\n') {
                        Map<String, Object> result = json.readValue(response.toByteArray(), new TypeReference<>() {});
                        if ("RUNTIME_CONFIG_INVALID".equals(result.get("error"))) {
                            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, String.valueOf(result.getOrDefault("message", "실행 환경 설정을 확인하세요.")));
                        }
                        if (result.containsKey("error")) throw new IOException("Runtime unavailable");
                        return result;
                    }
                    if (response.size() >= MAX_RESPONSE_BYTES) throw new IOException("Runtime response too large");
                    response.write(b);
                }
                input.clear();
            }
        } catch (IOException e) {
            throw new CustomException(ErrorCode.NO_SERVER_AVAILABLE, "코드 실행 환경을 사용할 수 없습니다. 잠시 후 다시 시도해 주세요.");
        }
    }

    private static void waitFor(Selector selector, long deadline) throws IOException {
        long remaining = deadline - System.nanoTime();
        if (remaining <= 0) throw new IOException("Runtime deadline exceeded");
        selector.select(Math.max(1, Math.min(1000, remaining / 1_000_000)));
        selector.selectedKeys().clear();
    }
}
