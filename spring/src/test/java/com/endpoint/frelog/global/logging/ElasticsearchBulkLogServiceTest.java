package com.endpoint.frelog.global.logging;

import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.web.client.RestClient;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;

class ElasticsearchBulkLogServiceTest {
    private HttpServer server;
    private final AtomicInteger requests = new AtomicInteger();

    @BeforeEach
    void startElasticsearchStub() throws IOException {
        server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext("/agora-logs/_bulk", exchange -> {
            exchange.getRequestBody().readAllBytes();
            requests.incrementAndGet();
            byte[] response = "{\"items\":[{\"create\":{\"status\":201}}]}"
                    .getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().set("Content-Type", "application/json");
            exchange.sendResponseHeaders(200, response.length);
            try (var output = exchange.getResponseBody()) {
                output.write(response);
            }
        });
        server.start();
    }

    @AfterEach
    void stopElasticsearchStub() {
        if (server != null) server.stop(0);
    }

    @Test
    void shutdownFlushesMoreThanTheScheduledBatchLimit() {
        ElasticsearchProperties properties = new ElasticsearchProperties();
        properties.setHost("127.0.0.1");
        properties.setPort(server.getAddress().getPort());
        properties.setLogIndex("agora-logs");
        properties.setLogPassword("test-writer-password");

        RestClient restClient = RestClient.builder().baseUrl(properties.getBaseUrl()).build();
        ElasticsearchBulkLogService service = new ElasticsearchBulkLogService(
                restClient, properties, new ObjectMapper(), 1);

        for (int index = 0; index < 11; index++) {
            service.record("test", "event", "INFO", "queued event", Map.of("sequence", index));
        }

        service.flushOnShutdown();

        assertEquals(11, requests.get());
    }
}
