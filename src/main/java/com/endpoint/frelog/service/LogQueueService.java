package com.endpoint.frelog.service;

import com.endpoint.frelog.dto.LogMessageDto;
import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.redis.core.RedisTemplate;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;

@Slf4j
@Service
@RequiredArgsConstructor
public class LogQueueService {

    public static final String LOG_QUEUE_KEY = "queue:application-logs";

    private final RedisTemplate<String, Object> redisTemplate;
    private final ObjectMapper objectMapper;

    /**
     * POST 요청으로 들어온 로그 데이터를 Redis List 큐에 임시 저장 (RPUSH)
     */
    public void enqueue(LogMessageDto message) {
        redisTemplate.opsForList().rightPush(LOG_QUEUE_KEY, message);
        log.debug("Enqueued log message to Redis queue: {}", message.getServiceName());
    }

    /**
     * Redis List 큐에서 최대 chunkSize 만큼의 로그를 순차적으로 인출 (LPOP)
     */
    public List<LogMessageDto> dequeueBatch(long chunkSize) {
        List<LogMessageDto> batch = new ArrayList<>();

        for (int i = 0; i < chunkSize; i++) {
            Object item = redisTemplate.opsForList().leftPop(LOG_QUEUE_KEY);
            if (item == null) {
                break;
            }

            try {
                LogMessageDto dto;
                if (item instanceof LogMessageDto logMessageDto) {
                    dto = logMessageDto;
                } else {
                    dto = objectMapper.convertValue(item, LogMessageDto.class);
                }
                batch.add(dto);
            } catch (Exception e) {
                log.error("Failed to deserialize queued log item: {}", item, e);
            }
        }

        return batch;
    }

    /**
     * 현재 큐에 대기 중인 로그 개수 조회
     */
    public long getQueueSize() {
        Long size = redisTemplate.opsForList().size(LOG_QUEUE_KEY);
        return size != null ? size : 0;
    }
}
