package com.endpoint.frelog.domain.user.entity;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.PrePersist;
import jakarta.persistence.PreUpdate;
import jakarta.persistence.Table;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;

import java.time.LocalDateTime;

@Entity
@Table(name = "users")
public class User {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    @Column(name = "user_id")
    @JdbcTypeCode(SqlTypes.INTEGER)
    private Long userId;

    @org.hibernate.annotations.Nationalized
    // Nullable tombstones require the filtered unique index installed by the SQL migration.
    @Column(name = "email", length = 255)
    private String email;

    @org.hibernate.annotations.Nationalized
    @Column(name = "password_hash", length = 255)
    private String passwordHash;

    @org.hibernate.annotations.Nationalized
    @Column(name = "nickname", nullable = false, length = 100)
    private String nickname;

    @Column(name = "tag_number", nullable = false)
    private Integer tagNumber;

    @org.hibernate.annotations.Nationalized
    @Enumerated(EnumType.STRING)
    @Column(name = "role", nullable = false, length = 20)
    private Role role = Role.ROLE_USER;

    @org.hibernate.annotations.Nationalized
    @Enumerated(EnumType.STRING)
    @Column(name = "status", nullable = false, length = 20)
    private UserStatus status = UserStatus.ACTIVE;

    @org.hibernate.annotations.Nationalized
    @Column(name = "oauth_provider", length = 50)
    private String oauthProvider;

    @org.hibernate.annotations.Nationalized
    @Column(name = "oauth_id", length = 255)
    private String oauthId;

    @Column(name="orcid",length=19) private String orcid;
    @Column(name="orcid_issuer",length=64) private String orcidIssuer;
    public String getOrcid(){return orcid;}
    public void setOrcid(String value){orcid=value;}
    public String getOrcidIssuer(){return orcidIssuer;}
    public void setOrcidIssuer(String value){orcidIssuer=value;}

    @Column(name="profile_image_url",length=512)
    private String profileImageUrl;
    public String getProfileImageUrl(){return profileImageUrl;}
    public void setProfileImageUrl(String value){profileImageUrl=value;}

    @Column(name = "email_verified_at")
    private LocalDateTime emailVerifiedAt;

    public LocalDateTime getEmailVerifiedAt() { return emailVerifiedAt; }
    public void setEmailVerifiedAt(LocalDateTime value) { emailVerifiedAt=value; }

    @Column(name = "password_changed_at")
    private LocalDateTime passwordChangedAt;

    @Column(name = "created_at", nullable = false, updatable = false)
    private LocalDateTime createdAt;

    @Column(name = "updated_at", nullable = false)
    private LocalDateTime updatedAt;

    public User() {
    }

    public User(String email, String passwordHash, String nickname, Integer tagNumber, Role role) {
        this.email = email;
        this.passwordHash = passwordHash;
        this.nickname = nickname;
        this.tagNumber = tagNumber;
        this.role = role != null ? role : Role.ROLE_USER;
        this.status = UserStatus.ACTIVE;
    }

    @PrePersist
    protected void onCreate() {
        LocalDateTime now = LocalDateTime.now();
        if (this.createdAt == null) {
            this.createdAt = now;
        }
        if (this.updatedAt == null) {
            this.updatedAt = now;
        }
        if (this.role == null) {
            this.role = Role.ROLE_USER;
        }
        if (this.status == null) {
            this.status = UserStatus.ACTIVE;
        }
    }

    @PreUpdate
    protected void onUpdate() {
        this.updatedAt = LocalDateTime.now();
    }


    public void changePassword(String newPasswordHash) {
        this.passwordHash = newPasswordHash;
        this.passwordChangedAt = LocalDateTime.now();
    }

    public void erasePersonalData() {
        if(status!=UserStatus.WITHDRAWN||userId==null)throw new IllegalStateException("Only persisted withdrawn accounts can be erased");
        orcid=null;orcidIssuer=null;email=null;passwordHash=null;oauthProvider=null;oauthId=null;profileImageUrl=null;emailVerifiedAt=null;passwordChangedAt=null;
        nickname="삭제된 계정";tagNumber=Math.toIntExact(userId);role=Role.ROLE_USER;
    }

    public boolean isActive() {
        return this.status == UserStatus.ACTIVE;
    }

    // Getters and Setters
    public Long getUserId() {
        return userId;
    }

    public void setUserId(Long userId) {
        this.userId = userId;
    }

    public String getEmail() {
        return email;
    }

    public void setEmail(String email) {
        this.email = email;
    }

    public String getPasswordHash() {
        return passwordHash;
    }

    public void setPasswordHash(String passwordHash) {
        this.passwordHash = passwordHash;
    }

    public String getNickname() {
        return nickname;
    }

    public void setNickname(String nickname) {
        this.nickname = nickname;
    }

    public Integer getTagNumber() {
        return tagNumber;
    }

    public void setTagNumber(Integer tagNumber) {
        this.tagNumber = tagNumber;
    }

    public Role getRole() {
        return role;
    }

    public void setRole(Role role) {
        this.role = role;
    }

    public UserStatus getStatus() {
        return status;
    }

    public void setStatus(UserStatus status) {
        this.status = status;
    }

    public String getOauthProvider() {
        return oauthProvider;
    }

    public void setOauthProvider(String oauthProvider) {
        this.oauthProvider = oauthProvider;
    }

    public String getOauthId() {
        return oauthId;
    }

    public void setOauthId(String oauthId) {
        this.oauthId = oauthId;
    }


    public LocalDateTime getPasswordChangedAt() {
        return passwordChangedAt;
    }

    public void setPasswordChangedAt(LocalDateTime passwordChangedAt) {
        this.passwordChangedAt = passwordChangedAt;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }

    public LocalDateTime getUpdatedAt() {
        return updatedAt;
    }

    public void setUpdatedAt(LocalDateTime updatedAt) {
        this.updatedAt = updatedAt;
    }

}
