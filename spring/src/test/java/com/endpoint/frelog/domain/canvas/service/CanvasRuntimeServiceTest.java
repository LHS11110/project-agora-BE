package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.global.exception.CustomException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.concurrent.CompletableFuture;

import static org.junit.jupiter.api.Assertions.*;

class CanvasRuntimeServiceTest {
    @TempDir Path directory;

    @Test void exchangesBoundedJsonWithLocalManager() throws Exception {
        Path socket = directory.resolve("manager.sock");
        try (ServerSocketChannel server = ServerSocketChannel.open(StandardProtocolFamily.UNIX)) {
            server.bind(UnixDomainSocketAddress.of(socket));
            CompletableFuture<String> request = CompletableFuture.supplyAsync(() -> {
                try (SocketChannel connection = server.accept()) {
                    ByteBuffer data = ByteBuffer.allocate(8192);
                    connection.read(data);
                    data.flip();
                    String text = StandardCharsets.UTF_8.decode(data).toString();
                    connection.write(StandardCharsets.UTF_8.encode("{\"exitCode\":0,\"output\":\"ok\\n\",\"timedOut\":false,\"truncated\":false}\n"));
                    return text;
                } catch (Exception error) { throw new RuntimeException(error); }
            });
            var result = new CanvasRuntimeService(socket.toString()).execute(42, "python", "print('ok')");
            assertEquals("ok\n", result.get("output"));
            assertTrue(request.get().contains("\"canvasId\":42"));
        }
    }

    @Test void invalidLanguageAndOversizeInputNeverConnect() {
        var service = new CanvasRuntimeService(directory.resolve("missing").toString());
        assertThrows(CustomException.class, () -> service.execute(1, "ruby", ""));
        assertThrows(CustomException.class, () -> service.execute(1, "python", "한".repeat(22000)));
        assertThrows(CustomException.class, () -> service.execute(1, "python", "print(1)"));
    }
}
