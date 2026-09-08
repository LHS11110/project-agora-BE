package com.endpoint.frelog.controller;

import com.endpoint.frelog.dto.LogMessageDto;
import com.endpoint.frelog.service.LogQueueService;
import lombok.RequiredArgsConstructor;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/v1/logs")
@RequiredArgsConstructor
public class LogController {

    private final LogQueueService logQueueService;

    /**
     * 임시 POST 엔드포인트: 단건 로그 접수 및 Redis 큐 적재
     */
    @PostMapping
    public ResponseEntity<Map<String, Object>> receiveLog(@RequestBody LogMessageDto logMessageDto) {
        logQueueService.enqueue(logMessageDto);

        return ResponseEntity.status(HttpStatus.ACCEPTED).body(Map.of(
                "status", "ACCEPTED",
                "message", "Log queued successfully in Redis buffer.",
                "serviceName", logMessageDto.getServiceName() != null ? logMessageDto.getServiceName() : "unknown"
        ));
    }

    /**
     * 다건(Bulk) 로그 접수 및 Redis 큐 적재
     */
    @PostMapping("/bulk")
    public ResponseEntity<Map<String, Object>> receiveBulkLogs(@RequestBody List<LogMessageDto> logMessageDtos) {
        for (LogMessageDto dto : logMessageDtos) {
            logQueueService.enqueue(dto);
        }

        return ResponseEntity.status(HttpStatus.ACCEPTED).body(Map.of(
                "status", "ACCEPTED",
                "message", "Bulk logs queued successfully in Redis buffer.",
                "count", logMessageDtos.size()
        ));
    }

    /**
     * 현재 Redis 버퍼 큐 상태 확인
     */
    @GetMapping("/queue-size")
    public ResponseEntity<Map<String, Object>> getQueueSize() {
        long size = logQueueService.getQueueSize();
        return ResponseEntity.ok(Map.of(
                "queueKey", LogQueueService.LOG_QUEUE_KEY,
                "pendingCount", size
        ));
    }
}
