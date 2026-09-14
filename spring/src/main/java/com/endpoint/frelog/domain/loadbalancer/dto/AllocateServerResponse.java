package com.endpoint.frelog.domain.loadbalancer.dto;

public record AllocateServerResponse(
        String ip,
        String port,
        String serverIp,
        String serverPort
) {
    public static AllocateServerResponse of(String ip, String port) {
        return new AllocateServerResponse(ip, port, ip, port);
    }
}
