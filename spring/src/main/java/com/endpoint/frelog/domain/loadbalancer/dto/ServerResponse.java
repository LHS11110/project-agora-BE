package com.endpoint.frelog.domain.loadbalancer.dto;

import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;

public record ServerResponse(
        Integer serverId,
        String serverIp,
        String serverPort,
        String wsPort,
        String serverName,
        Boolean isActive,
        Integer currentLoad
) {
    public static ServerResponse from(ServerInfo server, Integer currentLoad) {
        return new ServerResponse(
                server.getServerId(),
                server.getServerIp(),
                server.getServerPort(),
                server.getWsPort(),
                server.getServerName(),
                server.getIsActive(),
                currentLoad
        );
    }
}
