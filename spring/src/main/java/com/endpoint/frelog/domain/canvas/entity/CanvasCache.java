package com.endpoint.frelog.domain.canvas.entity;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.PrePersist;
import jakarta.persistence.PreUpdate;
import jakarta.persistence.Table;

import java.time.LocalDateTime;

@Entity
@Table(name = "canvas_cache")
public class CanvasCache {

    @Id
    @Column(name = "canvas_id", nullable = false)
    private Integer canvasId;

    @Column(name = "canvas_name", nullable = false, length = 255)
    private String canvasName;

    @Column(name = "redis_ip", length = 45)
    private String redisIp;

    @Column(name = "redis_port", length = 10)
    private String redisPort;

    @Column(name = "server_ip", length = 45)
    private String serverIp;

    @Column(name = "server_port", length = 10)
    private String serverPort;

    @Column(name = "is_cached", nullable = false)
    private Boolean isCached = false;

    @Column(name = "created_at", nullable = false, updatable = false)
    private LocalDateTime createdAt;

    @Column(name = "updated_at", nullable = false)
    private LocalDateTime updatedAt;

    public CanvasCache() {
    }

    public CanvasCache(Integer canvasId, String canvasName) {
        this.canvasId = canvasId;
        this.canvasName = canvasName;
        // 요구사항: 처음에 cache에 redis 및 server는 항상 none(null)으로 시작하고 is_cached는 false
        this.redisIp = null;
        this.redisPort = null;
        this.serverIp = null;
        this.serverPort = null;
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

    public void updateCacheState(Boolean isCached, String redisIp, String redisPort, String serverIp, String serverPort) {
        if (isCached != null) {
            this.isCached = isCached;
        }
        this.redisIp = redisIp;
        this.redisPort = redisPort;
        this.serverIp = serverIp;
        this.serverPort = serverPort;
    }

    // Getters and Setters
    public Integer getCanvasId() {
        return canvasId;
    }

    public void setCanvasId(Integer canvasId) {
        this.canvasId = canvasId;
    }

    public String getCanvasName() {
        return canvasName;
    }

    public void setCanvasName(String canvasName) {
        this.canvasName = canvasName;
    }

    public String getRedisIp() {
        return redisIp;
    }

    public void setRedisIp(String redisIp) {
        this.redisIp = redisIp;
    }

    public String getRedisPort() {
        return redisPort;
    }

    public void setRedisPort(String redisPort) {
        this.redisPort = redisPort;
    }

    public String getServerIp() {
        return serverIp;
    }

    public void setServerIp(String serverIp) {
        this.serverIp = serverIp;
    }

    public String getServerPort() {
        return serverPort;
    }

    public void setServerPort(String serverPort) {
        this.serverPort = serverPort;
    }

    public Boolean getIsCached() {
        return isCached;
    }

    public void setIsCached(Boolean isCached) {
        this.isCached = isCached;
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
