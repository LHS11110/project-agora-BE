package com.endpoint.frelog.domain.irade;

import jakarta.persistence.*;
import java.time.LocalDateTime;

@Entity @Table(name="auth_orcid_attempts")
public class OrcidAttempt {
    @Id @Column(length=64) public String id;
    @Column(name="browser_hash",nullable=false,length=64) public String browserHash;
    @Column(name="target_user_id") public Long targetUserId;
    @Column(name="workspace_id") public Integer workspaceId;
    @Column(name="expires_at",nullable=false) public LocalDateTime expiresAt;
    @Column(nullable=false) public boolean consumed;
    public OrcidAttempt(){}
}
