package com.endpoint.frelog.domain.canvas.dto;

import com.endpoint.frelog.domain.canvas.entity.CanvasCache;

import java.time.LocalDateTime;

public record CanvasResponse(
        Integer canvasId,
        String canvasName,
        String redisIp,
        String redisPort,
        String serverIp,
        String serverPort,
        Boolean isCached,
        LocalDateTime createdAt,
        LocalDateTime updatedAt
) {
    public static CanvasResponse from(CanvasCache entity) {
        return new CanvasResponse(
                entity.getCanvasId(),
                entity.getCanvasName(),
                entity.getRedisIp(),
                entity.getRedisPort(),
                entity.getServerIp(),
                entity.getServerPort(),
                entity.getIsCached(),
                entity.getCreatedAt(),
                entity.getUpdatedAt()
        );
    }
}
