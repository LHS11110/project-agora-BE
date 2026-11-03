package com.endpoint.frelog.domain.auth.dto;

public record LoginResponse(
        String tokenType,
        String accessToken,
        UserResponse user,
        boolean mfaRequired,
        String challengeToken
) {
    public static LoginResponse challenge(String token) { return new LoginResponse(null, null, null, true, token); }
    public static LoginResponse of(String accessToken, UserResponse user) {
        return new LoginResponse("Bearer", accessToken, user, false, null);
    }
}
