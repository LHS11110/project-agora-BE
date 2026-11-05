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
            var result = new CanvasRuntimeService(socket.toString()).execute(42, "python", "print(input())", "first\nsecond\n");
            assertEquals("ok\n", result.get("output"));
            assertTrue(request.get().contains("\"canvasId\":42"));
            assertTrue(request.get().contains("\"stdin\":\"first\\nsecond\\n\""));
        }
    }

    @Test void invalidLanguageAndOversizeInputNeverConnect() {
        var service = new CanvasRuntimeService(directory.resolve("missing").toString());
        assertThrows(CustomException.class, () -> service.execute(1, "ruby", ""));
        var oversize = assertThrows(CustomException.class, () -> service.execute(1, "python", "input()", "한".repeat(22000)));
        assertTrue(oversize.getMessage().contains("64KB"));
        assertThrows(CustomException.class, () -> service.execute(1, "python", "한".repeat(22000)));
        assertThrows(CustomException.class, () -> service.execute(1, "python", "print(1)"));
    }
    @Test void streamingOutputArrivesBeforeExecutionCompletes() throws Exception {
        Path socket = directory.resolve("stream.sock");
        var firstOutput = new java.util.concurrent.CountDownLatch(1);
        try (ServerSocketChannel server = ServerSocketChannel.open(StandardProtocolFamily.UNIX)) {
            server.bind(UnixDomainSocketAddress.of(socket));
            var request = CompletableFuture.supplyAsync(() -> {
                try (SocketChannel connection = server.accept()) {
                    ByteBuffer data = ByteBuffer.allocate(8192);connection.read(data);data.flip();
                    String body = StandardCharsets.UTF_8.decode(data).toString();
                    connection.write(StandardCharsets.UTF_8.encode("{\"type\":\"output\",\"output\":\"first\\n\"}\n"));
                    assertTrue(firstOutput.await(2, java.util.concurrent.TimeUnit.SECONDS));
                    connection.write(StandardCharsets.UTF_8.encode("{\"exitCode\":0,\"output\":\"first\\nlast\\n\"}\n"));
                    return body;
                } catch (Exception error) { throw new RuntimeException(error); }
            });
            var seen = new java.util.ArrayList<String>();
            var result = new CanvasRuntimeService(socket.toString()).executeStream(42,"python","print(1)",null, frame -> {
                seen.add((String) frame.get("output"));firstOutput.countDown();
            });
            assertEquals(java.util.List.of("first\n"), seen);
            assertEquals("first\nlast\n", result.get("output"));
            assertTrue(request.get().contains("\"stream\":true"));
        }
    }


    @Test void terminalRejectsInvalidInputBeforeOpeningSocket() {
        var service=new CanvasRuntimeService(directory.resolve("absent").toString());
        assertThrows(CustomException.class,()->service.terminal(1,7L,"../bad","open","",80,24,0L));
        assertThrows(CustomException.class,()->service.terminal(1,7L,"term","exec","",80,24,0L));
        assertThrows(CustomException.class,()->service.terminal(1,7L,"term","input","한".repeat(3000),80,24,0L));
        assertThrows(CustomException.class,()->service.terminal(1,7L,"term","resize","",0,24,0L));
    }
}
