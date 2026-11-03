package com.endpoint.frelog.domain.auth.email;
import jakarta.persistence.*;
import java.time.LocalDateTime;
@Entity @Table(name="auth_email_verification")
public class EmailProof {
 @Id @org.hibernate.annotations.Nationalized @Column(length=255) String email;
 @Column(length=64) String challengeHash;
 @Column(length=64) String codeHash;
 LocalDateTime expiresAt;
 int attempts;
 LocalDateTime lastSentAt;
 LocalDateTime windowStart;
 int sends;
 @Column(length=64) String grantHash;
 LocalDateTime grantExpiresAt;
 boolean consumed;
 protected EmailProof(){}
 EmailProof(String email){this.email=email;}
}
