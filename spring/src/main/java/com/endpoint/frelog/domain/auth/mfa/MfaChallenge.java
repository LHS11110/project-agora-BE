package com.endpoint.frelog.domain.auth.mfa;
import jakarta.persistence.*;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import java.time.LocalDateTime;
@Entity @Table(name="auth_mfa_challenges")
public class MfaChallenge {
    @Id @Column(length=64) String id;
    @JdbcTypeCode(SqlTypes.INTEGER) Long userId;
    @Column(length=36) String version;
    @Column(length=64) String clientIp;
    LocalDateTime expiresAt;
    boolean used;
    protected MfaChallenge(){}
}
