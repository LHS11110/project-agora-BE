package com.endpoint.frelog.domain.user.entity;

import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
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
import jakarta.persistence.Transient;
import org.springframework.data.domain.Persistable;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;

import java.time.LocalDateTime;

@Entity
@Table(name = "user_sessions")
public class UserSession implements Persistable<Long> {

    @Id
    @Column(name = "user_id")
    @JdbcTypeCode(SqlTypes.INTEGER)
    private Long userId;

    @Transient
    private boolean isNew = true;

    @Override
    public boolean isNew() {
        return isNew;
    }

    @OneToOne
    @JoinColumn(name = "user_id", insertable = false, updatable = false)
    private User user;

    @ManyToOne
    @JoinColumn(name = "cpp_server_id")
    private ServerInfo cppServer;

    @ManyToOne
    @JoinColumn(name = "canvas_id")
    private CanvasInfo canvas;

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

    @jakarta.persistence.PostPersist
    @jakarta.persistence.PostLoad
    protected void markNotNew() {
        this.isNew = false;
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

    @Override
    public Long getId() {
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

    public CanvasInfo getCanvas() {
        return canvas;
    }

    public void setCanvas(CanvasInfo canvas) {
        this.canvas = canvas;
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
