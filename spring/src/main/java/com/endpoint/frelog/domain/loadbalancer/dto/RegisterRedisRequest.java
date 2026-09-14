package com.endpoint.frelog.domain.loadbalancer.dto;

import jakarta.validation.constraints.NotBlank;

public record RegisterRedisRequest(
        @NotBlank(message = "Redis IP는 필수입니다.")
        String redisIp,

        @NotBlank(message = "Redis 포트는 필수입니다.")
        String redisPort,

        String redisName
) {
}
