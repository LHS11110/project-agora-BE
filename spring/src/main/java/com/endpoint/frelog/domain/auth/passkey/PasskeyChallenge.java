package com.endpoint.frelog.domain.auth.passkey;

import jakarta.persistence.*;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import java.time.LocalDateTime;

@Entity @Table(name="auth_passkey_challenges")
public class PasskeyChallenge {
    @Id @Column(length=64) String id;
    @Column(length=12,nullable=false) String purpose;
    @JdbcTypeCode(SqlTypes.INTEGER) Long userId;
    @Column(length=64,nullable=false) String clientIp;
    @Column(length=16384,nullable=false) String requestJson;
    @Column(nullable=false) LocalDateTime createdAt;
    @Column(nullable=false) LocalDateTime expiresAt;
    @Column(nullable=false) boolean used;
    protected PasskeyChallenge() {}
}
