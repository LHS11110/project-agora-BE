package com.endpoint.frelog.domain.auth.qr;
import jakarta.persistence.*;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import java.time.LocalDateTime;
@Entity @Table(name="auth_qr_logins")
public class QrLogin {
 @Id @Column(length=36) String id;
 @Column(length=64,nullable=false) String pollHash;
 @Column(length=64,nullable=false) String approveHash;
 @Column(length=64,nullable=false) String browserHash;
 @Column(length=6,nullable=false) String displayCode;
 @Column(length=64,nullable=false) String requesterIp;
 @Column(length=120,nullable=false) String deviceLabel;
 @Column(length=12,nullable=false) String status="PENDING";
 @Column(nullable=false) LocalDateTime createdAt;
 @Column(nullable=false) LocalDateTime expiresAt;
 LocalDateTime lastPoll;
 @JdbcTypeCode(SqlTypes.INTEGER) Long userId;
 @Column(length=36) String mfaVersion;
 @Column(nullable=false) int failures;
 protected QrLogin(){}
}
