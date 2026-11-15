package com.endpoint.frelog.domain.canvas.entity;

import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;
import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;

import java.time.LocalDateTime;

/**
 * Common metadata contract implemented by both {@link CanvasInfo} and {@link WorkspaceInfo}.
 */
public interface SpaceInfo {
    Integer getSpaceId();
    void setSpaceId(Integer id);
    RedisInfo getRedisInfo();
    void setRedisInfo(RedisInfo redisInfo);
    ServerInfo getCppServer();
    void setCppServer(ServerInfo cppServer);
    Boolean getIsCached();
    void setIsCached(Boolean isCached);
    String getServerIp();
    String getServerPort();
    String getRedisIp();
    String getRedisPort();
    LocalDateTime getCreatedAt();
    LocalDateTime getUpdatedAt();
    void updateCacheState(Boolean isCached, RedisInfo redisInfo, ServerInfo cppServer);
}
