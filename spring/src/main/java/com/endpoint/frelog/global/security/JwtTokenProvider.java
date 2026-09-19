package com.endpoint.frelog.global.security;

import io.jsonwebtoken.Claims;
import io.jsonwebtoken.ExpiredJwtException;
import io.jsonwebtoken.JwtException;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.security.Keys;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import javax.crypto.SecretKey;
import java.nio.charset.StandardCharsets;
import java.util.Date;

@Component
public class JwtTokenProvider {

    private static final Logger log = LoggerFactory.getLogger(JwtTokenProvider.class);

    private final SecretKey key;
    private final long expirationMs;

    public JwtTokenProvider(
            @Value("${jwt.secret:testSecretKey~c29tZS12ZXJ5LXNlY3VyZS1hbmQtbG9uZy1zZWNyZXQta2V5LWZvci1hZ29yYS1qd3QtYXV0aC0yMDI2}") String secret,
            @Value("${jwt.expiration-ms:86400000}") long expirationMs) {
        // JJWT HMAC-SHA algorithms require at least 256 bits (32 bytes)
        byte[] keyBytes = secret.getBytes(StandardCharsets.UTF_8);
        if (keyBytes.length < 32) {
            byte[] padded = new byte[32];
            System.arraycopy(keyBytes, 0, padded, 0, Math.min(keyBytes.length, 32));
            this.key = Keys.hmacShaKeyFor(padded);
        } else {
            this.key = Keys.hmacShaKeyFor(keyBytes);
        }
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
                .signWith(key)
                .compact();
    }

    public String createCanvasAccessToken(String nickname, Integer tagNumber, Integer canvasId, String clientIp, String serverHash) {
        Date now = new Date();
        // 캔버스 접속 토큰은 비교적 짧은 유효시간(예: 5분)을 가질 수 있지만 여기서는 편의상 동일하게 부여
        Date validity = new Date(now.getTime() + expirationMs);

        return Jwts.builder()
                .subject("canvas-access")
                .claim("nickname", nickname)
                .claim("tagNumber", tagNumber)
                .claim("canvasId", canvasId)
                .claim("clientIp", clientIp)
                .claim("serverHash", serverHash)
                .issuedAt(now)
                .expiration(validity)
                .signWith(key)
                .compact();
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
