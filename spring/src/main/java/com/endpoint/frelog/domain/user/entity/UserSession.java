package com.endpoint.frelog.domain.user.entity;

import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToOne;
import jakarta.persistence.MapsId;
import jakarta.persistence.PrePersist;
import jakarta.persistence.PreUpdate;
import jakarta.persistence.Table;

import java.time.LocalDateTime;

@Entity
@Table(name = "user_sessions")
public class UserSession {

    @Id
    @Column(name = "user_id")
    private Long userId;

    @OneToOne
    @JoinColumn(name = "user_id", insertable = false, updatable = false)
    private User user;

    @ManyToOne
    @JoinColumn(name = "cpp_server_id")
    private ServerInfo cppServer;

    @Column(name = "is_accessed", nullable = false)
    private Boolean isAccessed = false;

    @Column(name = "last_login_at")
    private LocalDateTime lastLoginAt;

    @Column(name = "updated_at", nullable = false)
    private LocalDateTime updatedAt;

    public UserSession() {}

    public UserSession(User user) {
        this.user = user;
        this.userId = user.getUserId();
    }

    @PrePersist
    protected void onCreate() {
        if (this.isAccessed == null) {
            this.isAccessed = false;
        }
        if (this.updatedAt == null) {
            this.updatedAt = LocalDateTime.now();
        }
    }

    @PreUpdate
    protected void onUpdate() {
        this.updatedAt = LocalDateTime.now();
    }

    public void updateLastLogin(LocalDateTime loginTime) {
        this.lastLoginAt = loginTime;
    }

    public Long getUserId() {
        return userId;
    }

    public void setUserId(Long userId) {
        this.userId = userId;
    }

    public User getUser() {
        return user;
    }

    public void setUser(User user) {
        this.user = user;
        this.userId = user != null ? user.getUserId() : null;
    }

    public ServerInfo getCppServer() {
        return cppServer;
    }

    public void setCppServer(ServerInfo cppServer) {
        this.cppServer = cppServer;
    }

    public Boolean getIsAccessed() {
        return isAccessed;
    }

    public void setIsAccessed(Boolean accessed) {
        isAccessed = accessed;
    }

    public LocalDateTime getLastLoginAt() {
        return lastLoginAt;
    }

    public void setLastLoginAt(LocalDateTime lastLoginAt) {
        this.lastLoginAt = lastLoginAt;
    }

    public LocalDateTime getUpdatedAt() {
        return updatedAt;
    }

    public void setUpdatedAt(LocalDateTime updatedAt) {
        this.updatedAt = updatedAt;
    }
}
