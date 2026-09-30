package com.endpoint.frelog.global.security;

import io.jsonwebtoken.Claims;
import io.jsonwebtoken.ExpiredJwtException;
import io.jsonwebtoken.JwtException;
import io.jsonwebtoken.Jwts;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;
import org.slf4j.MDC;

import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.util.Date;

@Component
public class JwtTokenProvider {

    private static final Logger log = LoggerFactory.getLogger(JwtTokenProvider.class);
    private static final long CANVAS_PASSWORD_GRANT_EXPIRATION_MS = 30 * 60 * 1000L;

    private final SecretKey key;
    private final long expirationMs;

    public JwtTokenProvider(
            @Value("${jwt.secret}") String secret,
            @Value("${jwt.expiration-ms:86400000}") long expirationMs) {
        // Match the C++ server's HS256 verifier for every supported key length.
        byte[] keyBytes = secret.getBytes(StandardCharsets.UTF_8);
        if (keyBytes.length < 32) {
            throw new IllegalArgumentException("JWT_SECRET must be at least 32 bytes long");
        }
        this.key = new SecretKeySpec(keyBytes, "HmacSHA256");
        this.expirationMs = expirationMs;
    }

    public String createToken(String email, Integer tagNumber, String nickname, String role, String clientIp) {
        Date now = new Date();
        Date validity = new Date(now.getTime() + expirationMs);

        return Jwts.builder()
                .subject(email)
                .claim("tagNumber", tagNumber)
                .claim("nickname", nickname)
                .claim("role", role)
                .claim("clientIp", clientIp)
                .issuedAt(now)
                .expiration(validity)
                .signWith(key, Jwts.SIG.HS256)
                .compact();
    }

    public String createCanvasAccessToken(String nickname, Integer tagNumber, Integer canvasId,
                                          String clientIp, String serverHash, long settingsRevision) {
        Date now = new Date();
        // 캔버스 접속 토큰은 비교적 짧은 유효시간(예: 5분)을 가질 수 있지만 여기서는 편의상 동일하게 부여
        Date validity = new Date(now.getTime() + expirationMs);

        var builder = Jwts.builder()
                .subject("canvas-access")
                .claim("nickname", nickname)
                .claim("tagNumber", tagNumber)
                .claim("canvasId", canvasId)
                .claim("clientIp", clientIp)
                .claim("serverHash", serverHash)
                .claim("settingsRevision", settingsRevision)
                .issuedAt(now)
                .expiration(validity);
        String requestId = MDC.get("request_id");
        if (requestId != null && requestId.matches("[A-Za-z0-9._:-]{1,64}")) {
            builder.claim("requestId", requestId);
        }
        return builder.signWith(key, Jwts.SIG.HS256).compact();
    }

    public String createCanvasPasswordGrant(Long userId, Integer canvasId, long settingsRevision) {
        Date now = new Date();
        Date validity = new Date(now.getTime() + CANVAS_PASSWORD_GRANT_EXPIRATION_MS);

        return Jwts.builder()
                .subject("canvas-password-grant")
                .claim("userId", userId)
                .claim("canvasId", canvasId)
                .claim("settingsRevision", settingsRevision)
                .issuedAt(now)
                .expiration(validity)
                .signWith(key, Jwts.SIG.HS256)
                .compact();
    }

    public boolean validateCanvasPasswordGrant(String token, Long userId, Integer canvasId, long settingsRevision) {
        if (token == null || token.isBlank()) return false;

        try {
            Claims claims = Jwts.parser()
                    .verifyWith(key)
                    .build()
                    .parseSignedClaims(token)
                    .getPayload();
            Object tokenUserId = claims.get("userId");
            Object tokenCanvasId = claims.get("canvasId");
            Object tokenSettingsRevision = claims.get("settingsRevision");
            return "canvas-password-grant".equals(claims.getSubject())
                    && tokenUserId instanceof Number userIdNumber && userIdNumber.longValue() == userId
                    && tokenCanvasId instanceof Number canvasIdNumber && canvasIdNumber.intValue() == canvasId
                    && tokenSettingsRevision instanceof Number revisionNumber
                    && revisionNumber.longValue() == settingsRevision;
        } catch (JwtException | IllegalArgumentException e) {
            return false;
        }
    }

    public String getEmailFromToken(String token) {
        return getClaims(token).getSubject();
    }

    public String getClientIpFromToken(String token) {
        return getClaims(token).get("clientIp", String.class);
    }

    public Integer getTagNumberFromToken(String token) {
        Object tagNumber = getClaims(token).get("tagNumber");
        if (tagNumber instanceof Number number) {
            return number.intValue();
        }
        return null;
    }

    public boolean validateToken(String token) {
        try {
            Jwts.parser()
                    .verifyWith(key)
                    .build()
                    .parseSignedClaims(token);
            return true;
        } catch (ExpiredJwtException e) {
            log.warn("만료된 JWT 토큰입니다: {}", e.getMessage());
        } catch (JwtException | IllegalArgumentException e) {
            log.warn("유효하지 않은 JWT 토큰입니다: {}", e.getMessage());
        }
        return false;
    }

    private Claims getClaims(String token) {
        return Jwts.parser()
                .verifyWith(key)
                .build()
                .parseSignedClaims(token)
                .getPayload();
    }
}
