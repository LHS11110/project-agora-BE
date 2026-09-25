package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.logging.ElasticsearchBulkLogService;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

@ExtendWith(MockitoExtension.class)
class CanvasRedisDocumentReaderFailoverTest {

    private static final String SENTINEL_USER = "sentinel-reader-test";
    private static final String SENTINEL_PASSWORD = "sentinel-test-password";

    @Mock
    private ElasticsearchBulkLogService logService;

    @Test
    void recoversWhenSentinelPromotesAnotherRedisPrimary() throws Exception {
        ObjectMapper objectMapper = new ObjectMapper();
        try (FakeSentinelCluster cluster = new FakeSentinelCluster(objectMapper)) {
            cluster.start();
            CanvasRedisDocumentReader reader = new CanvasRedisDocumentReader(
                    objectMapper,
                    "",
                    "test-password",
                    "127.0.0.1:" + cluster.sentinelPort(),
                    "agora-master",
                    SENTINEL_USER,
                    SENTINEL_PASSWORD,
                    logService);
            CanvasInfo canvasInfo = new CanvasInfo(42);

            assertThat(reader.read(canvasInfo).getCanvasName()).isEqualTo("primary-a");

            // Simulate loss of the active primary before Sentinel promotes node B.
            cluster.stopNodeA();
            assertThatThrownBy(() -> reader.read(canvasInfo))
                    .isInstanceOf(CustomException.class);

            cluster.promoteNodeB();
            assertThat(reader.read(canvasInfo).getCanvasName()).isEqualTo("primary-b");

            verify(logService).reportAvailability(eq("redis-sentinel"), eq(false), anyMap());
            verify(logService, times(2)).reportAvailability(eq("redis-sentinel"), eq(true), anyMap());
            assertThat(cluster.authenticatedSentinelQueries()).isPositive();
            verify(logService).reportPrimaryChange(eq("redis-sentinel"), eq(cluster.nodeAAddress()));
            verify(logService).reportPrimaryChange(eq("redis-sentinel"), eq(cluster.nodeBAddress()));
        }
    }

    private static final class FakeSentinelCluster implements AutoCloseable {
        private final FakeRedisNode nodeA;
        private final FakeRedisNode nodeB;
        private final FakeSentinel sentinel;

        private FakeSentinelCluster(ObjectMapper mapper) throws IOException {
            nodeA = new FakeRedisNode("primary-a", mapper);
            nodeB = new FakeRedisNode("primary-b", mapper);
            sentinel = new FakeSentinel(nodeA.port());
        }

        private void start() throws IOException {
            nodeA.start();
            nodeB.start();
            sentinel.start();
        }

        private int sentinelPort() {
            return sentinel.port();
        }

        private String nodeAAddress() {
            return "127.0.0.1:" + nodeA.port();
        }

        private String nodeBAddress() {
            return "127.0.0.1:" + nodeB.port();
        }

        private int authenticatedSentinelQueries() {
            return sentinel.authenticatedQueries();
        }

        private void stopNodeA() throws IOException {
            nodeA.close();
        }

        private void promoteNodeB() {
            sentinel.promote(nodeB.port());
        }

        @Override
        public void close() {
            sentinel.close();
            nodeA.close();
            nodeB.close();
        }
    }

    private abstract static class FakeRespServer implements AutoCloseable {
        private final ServerSocket server;
        private final AtomicBoolean running = new AtomicBoolean();
        private final ExecutorService clients = Executors.newCachedThreadPool(task -> {
            Thread thread = new Thread(task, "fake-redis-client");
            thread.setDaemon(true);
            return thread;
        });
        private Thread acceptThread;

        private FakeRespServer() throws IOException {
            server = new ServerSocket(0, 50, InetAddress.getLoopbackAddress());
        }

        final int port() {
            return server.getLocalPort();
        }

        final void start() {
            if (!running.compareAndSet(false, true)) return;
            acceptThread = new Thread(() -> {
                while (running.get()) {
                    try {
                        Socket socket = server.accept();
                        clients.execute(() -> handleClient(socket));
                    } catch (IOException e) {
                        if (running.get()) throw new IllegalStateException(e);
                    }
                }
            }, "fake-redis-accept");
            acceptThread.setDaemon(true);
            acceptThread.start();
        }

        private void handleClient(Socket socket) {
            try (socket) {
                socket.setSoTimeout(3000);
                serve(socket.getInputStream(), socket.getOutputStream());
            } catch (IOException ignored) {
                // Clients close after the tested command sequence or when a simulated node stops.
            }
        }

        abstract void serve(InputStream input, OutputStream output) throws IOException;

        @Override
        public void close() {
            running.set(false);
            try {
                server.close();
            } catch (IOException ignored) {
            }
            clients.shutdownNow();
        }
    }

    private static final class FakeSentinel extends FakeRespServer {
        private volatile int masterPort;
        private final java.util.concurrent.atomic.AtomicInteger authenticatedQueries =
                new java.util.concurrent.atomic.AtomicInteger();

        private FakeSentinel(int initialMasterPort) throws IOException {
            masterPort = initialMasterPort;
        }

        private void promote(int port) {
            masterPort = port;
        }

        private int authenticatedQueries() {
            return authenticatedQueries.get();
        }

        @Override
        void serve(InputStream input, OutputStream output) throws IOException {
            List<String> command = readCommand(input);
            if (command.size() != 3 || !"AUTH".equalsIgnoreCase(command.get(0))
                    || !SENTINEL_USER.equals(command.get(1))
                    || !SENTINEL_PASSWORD.equals(command.get(2))) {
                writeError(output, "Sentinel authentication required");
                return;
            }
            writeSimple(output, "OK");
            authenticatedQueries.incrementAndGet();
            command = readCommand(input);
            if (command.size() == 3 && "SENTINEL".equalsIgnoreCase(command.get(0))) {
                writeArray(output, List.of("127.0.0.1", Integer.toString(masterPort)));
            } else {
                writeError(output, "unexpected sentinel command");
            }
        }
    }

    private static final class FakeRedisNode extends FakeRespServer {
        private final String canvasName;
        private final String canvasJson;

        private FakeRedisNode(String canvasName, ObjectMapper mapper) throws IOException {
            this.canvasName = canvasName;
            CanvasDocument document = new CanvasDocument(canvasName, 42, 100L, null, "default");
            this.canvasJson = mapper.writeValueAsString(document);
        }

        @Override
        void serve(InputStream input, OutputStream output) throws IOException {
            while (true) {
                List<String> command = readCommand(input);
                if (command.isEmpty()) return;
                switch (command.get(0).toUpperCase()) {
                    case "AUTH" -> writeSimple(output, "OK");
                    case "ROLE" -> writeMasterRole(output);
                    case "JSON.GET" -> writeBulk(output, canvasJson);
                    default -> writeError(output, "unexpected redis command");
                }
            }
        }
    }

    private static List<String> readCommand(InputStream input) throws IOException {
        int marker = input.read();
        if (marker < 0) return List.of();
        if (marker != '*') throw new IOException("Expected RESP array");
        int count = Integer.parseInt(readLine(input));
        List<String> parts = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            if (input.read() != '$') throw new IOException("Expected RESP bulk string");
            int length = Integer.parseInt(readLine(input));
            byte[] value = input.readNBytes(length);
            if (value.length != length || input.read() != '\r' || input.read() != '\n') {
                throw new IOException("Truncated RESP command");
            }
            parts.add(new String(value, StandardCharsets.UTF_8));
        }
        return parts;
    }

    private static String readLine(InputStream input) throws IOException {
        ByteArrayOutputStream line = new ByteArrayOutputStream();
        int previous = -1;
        while (true) {
            int value = input.read();
            if (value < 0) throw new IOException("Unexpected EOF");
            if (previous == '\r' && value == '\n') {
                byte[] bytes = line.toByteArray();
                return new String(bytes, 0, bytes.length - 1, StandardCharsets.UTF_8);
            }
            line.write(value);
            previous = value;
        }
    }

    private static void writeSimple(OutputStream output, String value) throws IOException {
        output.write(("+" + value + "\r\n").getBytes(StandardCharsets.UTF_8));
        output.flush();
    }

    private static void writeError(OutputStream output, String value) throws IOException {
        output.write(("-ERR " + value + "\r\n").getBytes(StandardCharsets.UTF_8));
        output.flush();
    }

    private static void writeBulk(OutputStream output, String value) throws IOException {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        output.write(("$" + bytes.length + "\r\n").getBytes(StandardCharsets.UTF_8));
        output.write(bytes);
        output.write("\r\n".getBytes(StandardCharsets.UTF_8));
        output.flush();
    }

    private static void writeArray(OutputStream output, List<String> values) throws IOException {
        output.write(("*" + values.size() + "\r\n").getBytes(StandardCharsets.UTF_8));
        for (String value : values) {
            byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
            output.write(("$" + bytes.length + "\r\n").getBytes(StandardCharsets.UTF_8));
            output.write(bytes);
            output.write("\r\n".getBytes(StandardCharsets.UTF_8));
        }
        output.flush();
    }

    private static void writeMasterRole(OutputStream output) throws IOException {
        output.write("*3\r\n$6\r\nmaster\r\n:0\r\n*0\r\n".getBytes(StandardCharsets.UTF_8));
        output.flush();
    }
}
