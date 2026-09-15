package com.endpoint.frelog.domain.loadbalancer.dto;

public record AllocateServerResponse(
        String ip,
        String port,
        String wsPort,
        String serverIp,
        String serverPort
) {
    public static AllocateServerResponse of(String ip, String port, String wsPort) {
        return new AllocateServerResponse(ip, port, wsPort, ip, port);
    }
}
