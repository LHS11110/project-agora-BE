package com.endpoint.frelog.domain.auth.dto;

public record LoginResponse(
        String tokenType,
        String accessToken,
        UserResponse user
) {
    public static LoginResponse of(String accessToken, UserResponse user) {
        return new LoginResponse("Bearer", accessToken, user);
    }
}
