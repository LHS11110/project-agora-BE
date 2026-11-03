package com.endpoint.frelog.domain.auth.mfa;
import jakarta.persistence.*;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import java.time.LocalDateTime;
@Entity @Table(name="user_mfa")
public class MfaState {
    @Id @JdbcTypeCode(SqlTypes.INTEGER) Long userId;
    boolean enabled;
    @Column(length=512) String secret;
    @Column(length=512) String pendingSecret;
    LocalDateTime pendingExpires;
    long lastStep=-1;
    @Column(length=36) String version;
    @Column(length=2048) String recoveryHashes;
    int failures;
    LocalDateTime lockedUntil;
    protected MfaState(){}
    MfaState(Long id){userId=id;}
}
