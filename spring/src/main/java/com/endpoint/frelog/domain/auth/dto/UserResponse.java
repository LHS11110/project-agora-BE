package com.endpoint.frelog.domain.auth.dto;

import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.time.LocalDateTime;

public record UserResponse(
        @JsonProperty("user_id")
        Long userId,

        @JsonProperty("email")
        String email,

        @JsonProperty("nickname")
        String nickname,

        @JsonProperty("role")
        Role role,

        @JsonProperty("status")
        UserStatus status,

        @JsonProperty("state")
        String state,

        @JsonProperty("last_login_at")
        LocalDateTime lastLoginAt,

        @JsonProperty("password_chaged_at")
        LocalDateTime passwordChangedAt,

        @JsonProperty("created_at")
        LocalDateTime createdAt,

        @JsonProperty("updated_at")
        LocalDateTime updatedAt
) {
    public static UserResponse from(User user) {
        return new UserResponse(
                user.getUserId(),
                user.getEmail(),
                user.getNickname(),
                user.getRole(),
                user.getStatus(),
                user.getStatus() != null ? user.getStatus().name() : null,
                null, // lastLoginAt is now in UserSession
                user.getPasswordChangedAt(),
                user.getCreatedAt(),
                user.getUpdatedAt()
        );
    }

    // Additional getters for camelCase compatibility if needed by existing templates or Jackson
    @JsonGetter("password_changed_at")
    public LocalDateTime getPasswordChangedAtStandard() {
        return passwordChangedAt;
    }
}
