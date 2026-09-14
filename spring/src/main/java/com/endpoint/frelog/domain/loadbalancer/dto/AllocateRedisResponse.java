package com.endpoint.frelog.domain.loadbalancer.dto;

public record AllocateRedisResponse(
        String ip,
        String port,
        String redisIp,
        String redisPort
) {
    public static AllocateRedisResponse of(String ip, String port) {
        return new AllocateRedisResponse(ip, port, ip, port);
    }
}
