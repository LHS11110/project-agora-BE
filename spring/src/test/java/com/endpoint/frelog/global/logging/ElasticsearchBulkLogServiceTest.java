package com.endpoint.frelog.global.logging;

import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;
import org.springframework.web.client.RestClient;
import java.util.Map;
import java.util.HashSet;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.mockito.ArgumentMatchers.*;

class ElasticsearchBulkLogServiceTest {
    @Test
    void shutdownFlushesMoreThanTheScheduledBatchLimit() throws Exception {
        ElasticsearchProperties properties = new ElasticsearchProperties();
        properties.setLogIndex("agora-logs");
        properties.setLogPassword("test-writer-password");
        RestClient client = mock(RestClient.class);
        var request = mock(RestClient.RequestBodyUriSpec.class);
        var response = mock(RestClient.ResponseSpec.class);
        when(client.post()).thenReturn(request);
        when(request.uri("/{index}/_bulk?refresh=false", "agora-logs")).thenReturn(request);
        when(request.contentType(any())).thenReturn(request);
        when(request.body(any(Object.class))).thenReturn(request);
        when(request.retrieve()).thenReturn(response);
        when(response.body(String.class)).thenReturn("{\"items\":[{\"create\":{\"status\":201}}]}");
        var mapper = new ObjectMapper();
        var service = new ElasticsearchBulkLogService(client, properties, mapper, 1);
        for (int index = 0; index < 11; index++) {
            service.record("test", "event", "INFO", "queued event", Map.of("sequence", index));
        }
        service.flushOnShutdown();
        ArgumentCaptor<String> batches = ArgumentCaptor.forClass(String.class);
        verify(request, times(11)).body(batches.capture());
        var ids = new HashSet<String>();
        for (String batch : batches.getAllValues()) {
            assertTrue(batch.endsWith("\n"));
            String[] lines = batch.split("\n");
            assertEquals(2, lines.length);
            assertTrue(ids.add(mapper.readTree(lines[0]).at("/create/_id").asText()));
            assertTrue(mapper.readTree(lines[1]).isObject());
        }
        // A second shutdown must not re-send already acknowledged events.
        service.flushOnShutdown();
        verify(client, times(11)).post();
    }
}
