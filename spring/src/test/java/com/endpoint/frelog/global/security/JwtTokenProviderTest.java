package com.endpoint.frelog.global.security;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

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

        Integer tagNumber = 1234;
        String token = jwtTokenProvider.createToken(email, tagNumber, nickname, role, "127.0.0.1");

        // then
        assertThat(token).isNotBlank();
        assertThat(jwtTokenProvider.validateToken(token)).isTrue();
        assertThat(jwtTokenProvider.getEmailFromToken(token)).isEqualTo(email);
        assertThat(jwtTokenProvider.getTagNumberFromToken(token)).isEqualTo(tagNumber);
    }

    @Test
    @DisplayName("캔버스 토큰은 비밀키 길이에 관계없이 C++ 검증기와 동일한 HS256으로 서명한다")
    void canvasToken_UsesHs256ForSupportedKeyLengths() {
        for (String secret : List.of("0123456789abcdef0123456789abcdef", SECRET)) {
            JwtTokenProvider provider = new JwtTokenProvider(secret, EXPIRATION_MS);
            String token = provider.createCanvasAccessToken("테스터", 1234, 1, "127.0.0.1", "server-hash", 0L);
            String header = new String(Base64.getUrlDecoder().decode(token.split("\\.")[0]), StandardCharsets.UTF_8);
            String payload = new String(Base64.getUrlDecoder().decode(token.split("\\.")[1]), StandardCharsets.UTF_8);

            assertThat(header).contains("\"alg\":\"HS256\"");
            assertThat(payload).doesNotContain("\"userId\"");
            assertThat(provider.validateToken(token)).isTrue();
        }
    }

    @Test
    @DisplayName("32바이트 미만의 JWT 비밀키는 거부한다")
    void rejectsShortSecret() {
        assertThatThrownBy(() -> new JwtTokenProvider("short-secret", EXPIRATION_MS))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessageContaining("at least 32 bytes");
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
        String token = expiredProvider.createToken("test@agora.com", 1234, "테스터", "ROLE_USER", "127.0.0.1");

        // when & then
        assertThat(jwtTokenProvider.validateToken(token)).isFalse();
    }
    @Test
    void inheritedPasswordGrantsCannotExtendTheSourceExpiration() throws Exception {
        var key=new javax.crypto.spec.SecretKeySpec(SECRET.getBytes(StandardCharsets.UTF_8),"HmacSHA256");
        String source=io.jsonwebtoken.Jwts.builder().subject("canvas-password-grant").claim("userId",1L).claim("canvasId",10)
                .claim("settingsRevision",7L).expiration(new java.util.Date(System.currentTimeMillis()+120000)).signWith(key,io.jsonwebtoken.Jwts.SIG.HS256).compact();
        String child=jwtTokenProvider.createInheritedCanvasPasswordGrant(source,1L,20,7L);
        String back=jwtTokenProvider.createInheritedCanvasPasswordGrant(child,1L,10,7L);
        var mapper=new com.fasterxml.jackson.databind.ObjectMapper();
        long expiry=mapper.readTree(Base64.getUrlDecoder().decode(source.split("\\.")[1])).path("exp").asLong();
        assertThat(mapper.readTree(Base64.getUrlDecoder().decode(child.split("\\.")[1])).path("exp").asLong()).isEqualTo(expiry);
        assertThat(mapper.readTree(Base64.getUrlDecoder().decode(back.split("\\.")[1])).path("exp").asLong()).isEqualTo(expiry);
        assertThat(jwtTokenProvider.validateCanvasPasswordGrant(child,1L,20,7L)).isTrue();
        assertThat(jwtTokenProvider.validateCanvasPasswordGrant(child,1L,10,7L)).isFalse();
    }
    @Test void activeSessionProofCanRenewSignedExpiredGrantButNeverWrongScopeOrSignature() {
        var key=new javax.crypto.spec.SecretKeySpec(SECRET.getBytes(StandardCharsets.UTF_8),"HmacSHA256");
        String expired=io.jsonwebtoken.Jwts.builder().subject("canvas-password-grant").claim("userId",7).claim("canvasId",12).claim("settingsRevision",4)
            .issuedAt(new java.util.Date(System.currentTimeMillis()-3600000)).expiration(new java.util.Date(System.currentTimeMillis()-1000)).signWith(key,io.jsonwebtoken.Jwts.SIG.HS256).compact();
        assertThat(jwtTokenProvider.validateCanvasPasswordGrant(expired,7L,12,4)).isFalse();
        assertThat(jwtTokenProvider.validateActiveCanvasPasswordGrant(expired,7L,12,4)).isTrue();
        assertThat(jwtTokenProvider.validateActiveCanvasPasswordGrant(expired,8L,12,4)).isFalse();
        assertThat(jwtTokenProvider.validateActiveCanvasPasswordGrant(expired,7L,13,4)).isFalse();
        assertThat(jwtTokenProvider.validateActiveCanvasPasswordGrant(expired,7L,12,5)).isFalse();
        assertThat(jwtTokenProvider.validateActiveCanvasPasswordGrant(expired.substring(0,expired.lastIndexOf('.')+1)+"invalid",7L,12,4)).isFalse();
    }
}
