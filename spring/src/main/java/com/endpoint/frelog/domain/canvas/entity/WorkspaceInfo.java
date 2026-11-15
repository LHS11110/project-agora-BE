package com.endpoint.frelog.domain.canvas.entity;

import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;
import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import jakarta.persistence.*;
import java.time.LocalDateTime;
import java.time.ZoneOffset;

@Entity
@Table(name = "workspace_info")
public class WorkspaceInfo implements SpaceInfo {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    @Column(name = "workspace_id", nullable = false, updatable = false)
    private Integer workspaceId;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "redis_id")
    private RedisInfo redisInfo;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "cpp_server_id")
    private ServerInfo cppServer;

    @Column(name = "is_cached", nullable = false)
    private Boolean isCached = false;

    @Column(name = "created_at", nullable = false, updatable = false)
    private LocalDateTime createdAt;

    @Column(name = "updated_at", nullable = false)
    private LocalDateTime updatedAt;

    public WorkspaceInfo() {
    }

    public WorkspaceInfo(Integer workspaceId) {
        this.workspaceId = workspaceId;
    }

    @PrePersist
    protected void onCreate() {
        LocalDateTime now = LocalDateTime.now(ZoneOffset.UTC);
        if (this.createdAt == null) {
            this.createdAt = now;
        }
        this.updatedAt = now;
        if (this.isCached == null) {
            this.isCached = false;
        }
    }

    @PreUpdate
    protected void onUpdate() {
        this.updatedAt = LocalDateTime.now(ZoneOffset.UTC);
    }

    @Override
    public void updateCacheState(Boolean isCached, RedisInfo redisInfo, ServerInfo cppServer) {
        this.isCached = (isCached != null) ? isCached : false;
        this.redisInfo = redisInfo;
        this.cppServer = cppServer;
    }

    public Integer getWorkspaceId() {
        return workspaceId;
    }

    public void setWorkspaceId(Integer workspaceId) {
        this.workspaceId = workspaceId;
    }

    @Override
    public Integer getSpaceId() {
        return workspaceId;
    }

    @Override
    public void setSpaceId(Integer id) {
        this.workspaceId = id;
    }

    @Override
    public RedisInfo getRedisInfo() {
        return redisInfo;
    }

    @Override
    public void setRedisInfo(RedisInfo redisInfo) {
        this.redisInfo = redisInfo;
    }

    @Override
    public ServerInfo getCppServer() {
        return cppServer;
    }

    @Override
    public void setCppServer(ServerInfo cppServer) {
        this.cppServer = cppServer;
    }

    @Override
    public Boolean getIsCached() {
        return isCached;
    }

    @Override
    public void setIsCached(Boolean isCached) {
        this.isCached = isCached;
    }

    @Override
    public String getServerIp() {
        return (cppServer != null) ? cppServer.getServerIp() : null;
    }

    @Override
    public String getServerPort() {
        return (cppServer != null && cppServer.getServerPort() != null) ? String.valueOf(cppServer.getServerPort()) : null;
    }

    @Override
    public String getRedisIp() {
        return (redisInfo != null) ? redisInfo.getRedisIp() : null;
    }

    @Override
    public String getRedisPort() {
        return (redisInfo != null && redisInfo.getRedisPort() != null) ? String.valueOf(redisInfo.getRedisPort()) : null;
    }

    @Override
    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }

    @Override
    public LocalDateTime getUpdatedAt() {
        return updatedAt;
    }

    public void setUpdatedAt(LocalDateTime updatedAt) {
        this.updatedAt = updatedAt;
    }
}
