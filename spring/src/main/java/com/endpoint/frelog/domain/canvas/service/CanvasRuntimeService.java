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
        return execute(canvasId, language, code, null);
    }

    public Map<String, Object> execute(Integer canvasId, String language, String code, String stdin) {
        return executeRequest(canvasId, language, code, stdin, false);
    }

    public Map<String, Object> build(Integer canvasId, String language, String code) {
        return executeRequest(canvasId, language, code, null, true);
    }

    private Map<String, Object> executeRequest(Integer canvasId, String language, String code, String stdin, boolean build) {
        if (stdin != null && stdin.getBytes(StandardCharsets.UTF_8).length > 65536) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "표준 입력은 64KB 이하여야 합니다.");
        }
        if (language == null || !LANGUAGES.contains(language) || code == null ||
                code.getBytes(StandardCharsets.UTF_8).length > MAX_CODE_BYTES) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "지원하는 언어와 64KB 이하의 코드를 입력하세요.");
        }
        var payload = new java.util.HashMap<String, Object>();
        payload.put("canvasId", canvasId); payload.put("language", language); payload.put("code", code);
        if (stdin != null) payload.put("stdin", stdin);
        if (build) payload.put("action", "build");
        return exchange(payload);
    }

    public Map<String, Object> terminal(Integer canvasId, Long owner, String terminal, String operation,
            String data, Integer cols, Integer rows, Long offset) {
        if (terminal == null || !terminal.matches("[a-zA-Z0-9_-]{1,128}") || operation == null ||
                !Set.of("open","poll","input","resize","close").contains(operation) || data == null ||
                data.getBytes(StandardCharsets.UTF_8).length > 8192 || cols == null || cols < 2 || cols > 300 ||
                rows == null || rows < 2 || rows > 150 || offset == null || offset < 0) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"터미널 요청과 크기를 확인하세요.");
        }
        return exchange(Map.of("canvasId",canvasId,"action","terminal","owner",owner,"terminal",terminal,
                "operation",operation,"data",data,"cols",cols,"rows",rows,"offset",offset));
    }

    public TerminalBridge attachTerminal(Integer canvasId, Long owner, String terminal, int cols, int rows, long offset,
            java.util.function.Consumer<Map<String,Object>> receive, Runnable disconnected) throws IOException {
        return new TerminalBridge(Map.of("canvasId",canvasId,"action","terminal_attach","owner",owner,"terminal",terminal,
                "operation","open","data","","cols",cols,"rows",rows,"offset",offset),receive,disconnected);
    }

    public final class TerminalBridge implements AutoCloseable {
        private final SocketChannel channel;
        private final Object writer = new Object();
        private volatile boolean closed;
        TerminalBridge(Map<String,Object> initial, java.util.function.Consumer<Map<String,Object>> receive, Runnable disconnected) throws IOException {
            channel=SocketChannel.open(StandardProtocolFamily.UNIX);
            try { channel.connect(UnixDomainSocketAddress.of(socketPath));send(initial); }
            catch(IOException error){channel.close();throw error;}
            Thread.ofVirtual().name("terminal-output").start(()->{
                try {
                    var line=new ByteArrayOutputStream();var input=ByteBuffer.allocate(8192);
                    while(!closed){
                        int count=channel.read(input);if(count<0)break;input.flip();
                        while(input.hasRemaining()){
                            byte b=input.get();
                            if(b=='\n'){
                                Map<String,Object> frame=json.readValue(line.toByteArray(),new TypeReference<>(){});line.reset();
                                if(frame.containsKey("error"))throw new IOException("Terminal unavailable");
                                receive.accept(frame);
                            }else{if(line.size()>=100000)throw new IOException("Terminal frame too large");line.write(b);}
                        }
                        input.clear();
                    }
                }catch(Exception ignored){}finally{close();disconnected.run();}
            });
        }
        public void send(Map<String,Object> frame) throws IOException {
            byte[] bytes=(json.writeValueAsString(frame)+"\n").getBytes(StandardCharsets.UTF_8);
            if(bytes.length>60000)throw new IOException("Terminal input too large");
            synchronized(writer){if(closed)throw new IOException("Terminal closed");var buffer=ByteBuffer.wrap(bytes);while(buffer.hasRemaining())channel.write(buffer);}
        }
        public void close(){closed=true;try{channel.close();}catch(IOException ignored){}}
    }

    public Map<String,Object> notebook(Integer canvasId,String objectId,String operation,String code) {
        if(objectId==null||!objectId.matches("[a-zA-Z0-9_-]{1,128}")||!Set.of("prepare","execute","reset").contains(operation==null?"":operation)||code==null||code.getBytes(StandardCharsets.UTF_8).length>65536)
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"노트북 요청을 확인하세요.");
        return exchange(Map.of("canvasId",canvasId,"action","notebook","operation",operation,"notebook",objectId,"code",code));
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

    public Map<String, Object> executeStream(Integer canvasId, String language, String code, String stdin,
                                              java.util.function.Consumer<Map<String, Object>> onOutput) {
        if (language == null || !LANGUAGES.contains(language) || code == null || code.getBytes(StandardCharsets.UTF_8).length > MAX_CODE_BYTES ||
                (stdin != null && stdin.getBytes(StandardCharsets.UTF_8).length > 65536)) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "지원하는 언어와 64KB 이하의 코드·입력을 사용하세요.");
        }
        var payload = new java.util.HashMap<String, Object>();
        payload.put("canvasId", canvasId); payload.put("language", language); payload.put("code", code); payload.put("stream", true);
        if (stdin != null) payload.put("stdin", stdin);
        return exchange(payload, onOutput);
    }

    private Map<String, Object> exchange(Map<String, Object> payload) { return exchange(payload, null); }

    private Map<String, Object> exchange(Map<String, Object> payload, java.util.function.Consumer<Map<String,Object>> onOutput) {
        try (SocketChannel channel = SocketChannel.open(StandardProtocolFamily.UNIX);
             Selector selector = Selector.open()) {
            channel.configureBlocking(false);
            channel.connect(UnixDomainSocketAddress.of(socketPath));
            long deadline = System.nanoTime() + (("java_dependencies".equals(payload.get("action"))||("notebook".equals(payload.get("action"))&&"prepare".equals(payload.get("operation")))) ? 220_000_000_000L : "build".equals(payload.get("action")) ? 100_000_000_000L : 55_000_000_000L);
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
            int streamedBytes = 0;
            while (true) {
                int count = channel.read(input);
                if (count < 0) throw new IOException("Runtime disconnected");
                if (count == 0) { waitFor(selector, deadline); continue; }
                input.flip();
                while (input.hasRemaining()) {
                    byte b = input.get();
                    if (b == '\n') {
                        Map<String, Object> result = json.readValue(response.toByteArray(), new TypeReference<>() {});
                        response.reset();
                        if (onOutput != null && "output".equals(result.get("type"))) {
                            if (!(result.get("output") instanceof String text)) throw new IOException("Invalid output frame");
                            streamedBytes += text.getBytes(StandardCharsets.UTF_8).length;
                            if (streamedBytes > 196608) throw new IOException("Output stream too large");
                            onOutput.accept(result);
                            continue;
                        }
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
