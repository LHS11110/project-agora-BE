package com.endpoint.frelog.global.security;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.assertThat;

class JwtTokenProviderTest {

    private JwtTokenProvider jwtTokenProvider;
    private static final String SECRET = "this-is-a-very-long-secret-key-for-testing-jwt-token-provider-2026";
    private static final long EXPIRATION_MS = 60000; // 1 minute

    @BeforeEach
    void setUp() {
        jwtTokenProvider = new JwtTokenProvider(SECRET, EXPIRATION_MS);
    }

    @Test
    @DisplayName("JWT 토큰 생성 및 유효성 검증 성공")
    void createAndValidateToken_Success() {
        // given
        String email = "test@agora.com";
        Long userId = 1L;
        String nickname = "테스터";
        String role = "ROLE_USER";

        // when
        String token = jwtTokenProvider.createToken(email, userId, nickname, role);

        // then
        assertThat(token).isNotBlank();
        assertThat(jwtTokenProvider.validateToken(token)).isTrue();
        assertThat(jwtTokenProvider.getEmailFromToken(token)).isEqualTo(email);
        assertThat(jwtTokenProvider.getUserIdFromToken(token)).isEqualTo(userId);
    }

    @Test
    @DisplayName("유효하지 않은 JWT 토큰 검증 시 false 반환")
    void validateToken_Invalid() {
        // given
        String invalidToken = "invalid.token.value";

        // when & then
        assertThat(jwtTokenProvider.validateToken(invalidToken)).isFalse();
    }

    @Test
    @DisplayName("만료된 JWT 토큰 검증 시 false 반환")
    void validateToken_Expired() {
        // given: 만료 시간이 -1000ms인 provider
        JwtTokenProvider expiredProvider = new JwtTokenProvider(SECRET, -1000);
        String token = expiredProvider.createToken("test@agora.com", 1L, "테스터", "ROLE_USER");

        // when & then
        assertThat(jwtTokenProvider.validateToken(token)).isFalse();
    }
}
