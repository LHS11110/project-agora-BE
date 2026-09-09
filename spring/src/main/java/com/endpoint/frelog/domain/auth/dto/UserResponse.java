package com.endpoint.frelog.domain.auth.dto;

import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;

import java.time.LocalDateTime;

public record UserResponse(
        Long userId,
        String email,
        String nickname,
        Role role,
        UserStatus status,
        LocalDateTime lastLoginAt,
        LocalDateTime createdAt
) {
    public static UserResponse from(User user) {
        return new UserResponse(
                user.getUserId(),
                user.getEmail(),
                user.getNickname(),
                user.getRole(),
                user.getStatus(),
                user.getLastLoginAt(),
                user.getCreatedAt()
        );
    }
}
