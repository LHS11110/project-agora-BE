package com.endpoint.frelog.domain.canvas.dto;

import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;

import java.time.LocalDateTime;

public record CanvasResponse(
        Integer canvasId,
        String canvasName,
        Long userId,
        String userNickname,
        String redisIp,
        String redisPort,
        String serverIp,
        String serverPort,
        Boolean isCached,
        LocalDateTime createdAt,
        LocalDateTime updatedAt
) {
    public static CanvasResponse from(CanvasInfo entity) {
        Long ownerId = entity.getUser() != null ? entity.getUser().getUserId() : null;
        String ownerNickname = entity.getUser() != null ? entity.getUser().getNickname() : null;
        return new CanvasResponse(
                entity.getCanvasId(),
                entity.getCanvasName(),
                ownerId,
                ownerNickname,
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
