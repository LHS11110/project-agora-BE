package com.endpoint.frelog.service;

import com.endpoint.frelog.dto.LogMessageDto;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.data.redis.core.ListOperations;
import org.springframework.data.redis.core.RedisTemplate;

import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class LogQueueServiceTest {

    @Mock
    private RedisTemplate<String, Object> redisTemplate;

    @Mock
    private ListOperations<String, Object> listOperations;

    @Spy
    private ObjectMapper objectMapper = new ObjectMapper();

    @InjectMocks
    private LogQueueService logQueueService;

    @Test
    @DisplayName("enqueue 호출 시 Redis List에 rightPush 된다")
    void enqueue_Success() {
        // given
        when(redisTemplate.opsForList()).thenReturn(listOperations);
        LogMessageDto dto = LogMessageDto.builder()
                .serviceName("test-service")
                .message("hello world")
                .build();

        // when
        logQueueService.enqueue(dto);

        // then
        verify(listOperations, times(1)).rightPush(LogQueueService.LOG_QUEUE_KEY, dto);
    }

    @Test
    @DisplayName("dequeueBatch 호출 시 지정한 개수만큼 leftPop하여 반환한다")
    void dequeueBatch_Success() {
        // given
        when(redisTemplate.opsForList()).thenReturn(listOperations);
        LogMessageDto dto1 = LogMessageDto.builder().serviceName("service-1").build();
        LogMessageDto dto2 = LogMessageDto.builder().serviceName("service-2").build();

        when(listOperations.leftPop(LogQueueService.LOG_QUEUE_KEY))
                .thenReturn(dto1)
                .thenReturn(dto2)
                .thenReturn(null);

        // when
        List<LogMessageDto> batch = logQueueService.dequeueBatch(5);

        // then
        assertThat(batch).hasSize(2);
        assertThat(batch.get(0).getServiceName()).isEqualTo("service-1");
        assertThat(batch.get(1).getServiceName()).isEqualTo("service-2");
        verify(listOperations, times(3)).leftPop(LogQueueService.LOG_QUEUE_KEY);
    }
}
