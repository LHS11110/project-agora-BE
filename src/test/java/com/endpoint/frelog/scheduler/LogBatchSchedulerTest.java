package com.endpoint.frelog.scheduler;

import com.endpoint.frelog.dto.LogMessageDto;
import com.endpoint.frelog.repository.elasticsearch.LogElasticsearchRepository;
import com.endpoint.frelog.repository.jpa.LogJpaRepository;
import com.endpoint.frelog.service.LogQueueService;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.test.util.ReflectionTestUtils;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;

import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class LogBatchSchedulerTest {

    @Mock
    private LogQueueService logQueueService;

    @Mock
    private LogJpaRepository logJpaRepository;

    @Mock
    private LogElasticsearchRepository logElasticsearchRepository;

    @Spy
    private ObjectMapper objectMapper = new ObjectMapper();

    @InjectMocks
    private LogBatchScheduler logBatchScheduler;

    @BeforeEach
    void setUp() {
        ReflectionTestUtils.setField(logBatchScheduler, "chunkSize", 10L);
    }

    @Test
    @DisplayName("Redis 큐에 데이터가 있을 때 MS SQL과 Elasticsearch에 각각 일괄 저장이 호출된다")
    void processPendingLogs_Success() {
        // given
        when(logQueueService.getQueueSize()).thenReturn(2L);

        List<LogMessageDto> batch = List.of(
                LogMessageDto.builder()
                        .serviceName("payment-service")
                        .level("INFO")
                        .message("Payment approved")
                        .timestamp(LocalDateTime.now())
                        .metadata(Map.of("amount", 50000))
                        .build(),
                LogMessageDto.builder()
                        .serviceName("auth-service")
                        .level("WARN")
                        .message("Failed login attempt")
                        .timestamp(LocalDateTime.now())
                        .build()
        );
        when(logQueueService.dequeueBatch(10L)).thenReturn(batch);

        // when
        logBatchScheduler.processPendingLogs();

        // then
        verify(logJpaRepository, times(1)).saveAll(anyList());
        verify(logElasticsearchRepository, times(1)).saveAll(anyList());
    }

    @Test
    @DisplayName("Redis 큐가 비어있으면 저장 로직을 수행하지 않고 종료한다")
    void processPendingLogs_WhenQueueEmpty_DoNothing() {
        // given
        when(logQueueService.getQueueSize()).thenReturn(0L);

        // when
        logBatchScheduler.processPendingLogs();

        // then
        verify(logQueueService, never()).dequeueBatch(anyLong());
        verify(logJpaRepository, never()).saveAll(anyList());
        verify(logElasticsearchRepository, never()).saveAll(anyList());
    }
}
