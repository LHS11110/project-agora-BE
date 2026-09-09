package com.endpoint.frelog.domain.canvas.dto;

public record UpdateCanvasCacheRequest(
        Boolean isCached,
        String redisIp,
        String redisPort,
        String serverIp,
        String serverPort
) {
}
