package com.endpoint.frelog.domain.loadbalancer.dto;

import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;

public record RedisResponse(
        Integer redisId,
        String redisIp,
        String redisPort,
        String redisName,
        Boolean isActive,
        Integer currentLoad
) {
    public static RedisResponse from(RedisInfo redis, Integer currentLoad) {
        return new RedisResponse(
                redis.getRedisId(),
                redis.getRedisIp(),
                redis.getRedisPort(),
                redis.getRedisName(),
                redis.getIsActive(),
                currentLoad
        );
    }
}
