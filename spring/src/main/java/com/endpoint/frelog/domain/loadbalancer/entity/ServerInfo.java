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
@Table(name = "cpp_server")
public class ServerInfo {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    @Column(name = "server_id")
    private Integer serverId;

    @Column(name = "server_ip", nullable = false, length = 45)
    private String serverIp;

    @Column(name = "server_port", nullable = false, length = 10)
    private String serverPort;

    @Column(name = "is_activated", nullable = false)
    private Boolean isActivated = true;

    @Column(name = "created_at", nullable = false, updatable = false)
    private LocalDateTime createdAt;

    public ServerInfo() {
    }

    public ServerInfo(String serverIp, String serverPort) {
        this.serverIp = serverIp;
        this.serverPort = serverPort;
        this.isActivated = true;
    }

    public ServerInfo(String serverIp, String serverPort, String serverName) {
        this.serverIp = serverIp;
        this.serverPort = serverPort;
        this.isActivated = true;
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

    public Integer getServerId() {
        return serverId;
    }

    public void setServerId(Integer serverId) {
        this.serverId = serverId;
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

    // Backwards compatibility for serverName
    public String getServerName() {
        return "Server-" + serverPort;
    }

    public void setServerName(String serverName) {
    }
}
