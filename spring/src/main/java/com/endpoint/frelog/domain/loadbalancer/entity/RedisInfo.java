package com.endpoint.frelog.domain.loadbalancer.entity;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.PrePersist;
import jakarta.persistence.Table;

import java.time.LocalDateTime;

@Entity
@Table(name = "redis_server")
public class RedisInfo {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    @Column(name = "redis_id")
    private Long redisId;

    @Column(name = "redis_ip", nullable = false, length = 45)
    private String redisIp;

    @Column(name = "redis_port", nullable = false, length = 10)
    private String redisPort;

    @Column(name = "is_activated", nullable = false)
    private Boolean isActivated = true;

    @Column(name = "created_at", nullable = false, updatable = false)
    private LocalDateTime createdAt;

    public RedisInfo() {
    }

    public RedisInfo(String redisIp, String redisPort) {
        this.redisIp = redisIp;
        this.redisPort = redisPort;
        this.isActivated = true;
    }

    public RedisInfo(String redisIp, String redisPort, String redisName) {
        this.redisIp = redisIp;
        this.redisPort = redisPort;
        this.isActivated = true;
    }

    public String getRedisName() {
        return "Redis-" + redisPort;
    }

    public void setRedisName(String redisName) {
    }

    @PrePersist
    protected void onCreate() {
        if (this.createdAt == null) {
            this.createdAt = LocalDateTime.now();
        }
        if (this.isActivated == null) {
            this.isActivated = true;
        }
    }

    public Long getRedisId() {
        return redisId;
    }

    public void setRedisId(Long redisId) {
        this.redisId = redisId;
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

    public Boolean getIsActivated() {
        return isActivated;
    }

    public void setIsActivated(Boolean activated) {
        isActivated = activated;
    }

    // Backwards compatibility alias
    public Boolean getIsActive() {
        return isActivated;
    }

    public void setIsActive(Boolean active) {
        this.isActivated = active;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }
}
