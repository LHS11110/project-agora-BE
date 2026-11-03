package com.endpoint.frelog.domain.auth.passkey;

import jakarta.persistence.*;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import java.time.LocalDateTime;

@Entity @Table(name="user_passkeys")
public class PasskeyCredential {
    @Id @Column(length=36) String id;
    @Column(length=64,nullable=false,unique=true) String credentialHash;
    @Column(length=2048,nullable=false) String credentialId;
    @JdbcTypeCode(SqlTypes.INTEGER) @Column(nullable=false) Long userId;
    @Column(length=64,nullable=false) String userHandle;
    @Column(length=4096,nullable=false) String publicKey;
    @Column(nullable=false) long signatureCount;
    @org.hibernate.annotations.Nationalized @Column(length=80,nullable=false) String name;
    @Column(nullable=false) LocalDateTime createdAt;
    LocalDateTime lastUsedAt;
    protected PasskeyCredential() {}
}
