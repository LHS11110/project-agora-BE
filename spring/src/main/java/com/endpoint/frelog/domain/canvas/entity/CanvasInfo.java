package com.endpoint.frelog.domain.canvas.entity;

import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;
import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.PrePersist;
import jakarta.persistence.PreUpdate;
import jakarta.persistence.Table;

import java.time.LocalDateTime;

@Entity
@Table(name = "canvas_info")
public class CanvasInfo {

    @Id
    @jakarta.persistence.GeneratedValue(strategy = jakarta.persistence.GenerationType.IDENTITY)
    @Column(name = "canvas_id", nullable = false)
    private Integer canvasId;

    @ManyToOne
    @JoinColumn(name = "redis_id")
    private RedisInfo redisInfo;

    @ManyToOne
    @JoinColumn(name = "cpp_server_id")
    private ServerInfo cppServer;

    @Column(name = "is_cached", nullable = false)
    private Boolean isCached = false;

    @Column(name = "created_at", nullable = false, updatable = false)
    private LocalDateTime createdAt;

    @Column(name = "updated_at", nullable = false)
    private LocalDateTime updatedAt;

    public CanvasInfo() {
    }

    public CanvasInfo(Integer canvasId) {
        this.canvasId = canvasId;
        this.redisInfo = null;
        this.cppServer = null;
        this.isCached = false;
    }

    @PrePersist
    protected void onCreate() {
        LocalDateTime now = LocalDateTime.now();
        if (this.createdAt == null) {
            this.createdAt = now;
        }
        if (this.updatedAt == null) {
            this.updatedAt = now;
        }
        if (this.isCached == null) {
            this.isCached = false;
        }
    }

    @PreUpdate
    protected void onUpdate() {
        this.updatedAt = LocalDateTime.now();
    }

    public void updateCacheState(Boolean isCached, RedisInfo redisInfo, ServerInfo cppServer) {
        if (isCached != null) {
            this.isCached = isCached;
        }
        this.redisInfo = redisInfo;
        this.cppServer = cppServer;
    }

    // Getters and Setters
    public Integer getCanvasId() {
        return canvasId;
    }

    public void setCanvasId(Integer canvasId) {
        this.canvasId = canvasId;
    }

    public RedisInfo getRedisInfo() {
        return redisInfo;
    }

    public void setRedisInfo(RedisInfo redisInfo) {
        this.redisInfo = redisInfo;
    }

    public ServerInfo getCppServer() {
        return cppServer;
    }

    public void setCppServer(ServerInfo cppServer) {
        this.cppServer = cppServer;
    }

    public Boolean getIsCached() {
        return isCached;
    }

    public void setIsCached(Boolean isCached) {
        this.isCached = isCached;
    }

    public String getServerIp() {
        return cppServer != null ? cppServer.getServerIp() : null;
    }

    public String getServerPort() {
        return cppServer != null ? cppServer.getServerPort() : null;
    }

    public String getRedisIp() {
        return redisInfo != null ? redisInfo.getRedisIp() : null;
    }

    public String getRedisPort() {
        return redisInfo != null ? redisInfo.getRedisPort() : null;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }

    public LocalDateTime getUpdatedAt() {
        return updatedAt;
    }

    public void setUpdatedAt(LocalDateTime updatedAt) {
        this.updatedAt = updatedAt;
    }
}
