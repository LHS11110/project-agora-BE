package com.endpoint.frelog.domain.auth.session;

import jakarta.persistence.*;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import java.time.LocalDateTime;

/** One login. The browser holds only the raw refresh secret; the database keeps its hash. */
@Entity @Table(name="auth_sessions")
public class AuthSession {
    @Id @Column(length=36) String id;
    @JdbcTypeCode(SqlTypes.INTEGER) @Column(nullable=false) Long userId;
    @Column(length=64,nullable=false,unique=true) String refreshHash;
    // The hash rotated away most recently: presenting it again reveals a stolen refresh secret.
    @Column(length=64) String previousHash;
    LocalDateTime rotatedAt;
    @Column(length=64) String clientIp;
    @Column(length=36) String mfaVersion;
    @Column(nullable=false) LocalDateTime createdAt;
    @Column(nullable=false) LocalDateTime expiresAt;
    LocalDateTime revokedAt;
    protected AuthSession(){}
    public String getId(){return id;}
    public Long getUserId(){return userId;}
}
