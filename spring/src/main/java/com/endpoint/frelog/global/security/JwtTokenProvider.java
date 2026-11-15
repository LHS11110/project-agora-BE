package com.endpoint.frelog.global.security;

import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
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
    private static final long CANVAS_PASSWORD_GRANT_EXPIRATION_MS = 4 * 60 * 60 * 1000L;
    private static final long CANVAS_PASSWORD_GRANT_VERSION = 2L;

    private final SecretKey key;
    private final long expirationMs;
    private final com.endpoint.frelog.domain.user.repository.UserRepository users;

    public JwtTokenProvider(String secret,long expirationMs){this(secret,expirationMs,null);}

    @org.springframework.beans.factory.annotation.Autowired
    public JwtTokenProvider(
            @Value("${jwt.secret}") String secret,
            @Value("${jwt.expiration-ms:86400000}") long expirationMs,
            com.endpoint.frelog.domain.user.repository.UserRepository users) {
        // Match the C++ server's HS256 verifier for every supported key length.
        byte[] keyBytes = secret.getBytes(StandardCharsets.UTF_8);
        if (keyBytes.length < 32) {
            throw new IllegalArgumentException("JWT_SECRET must be at least 32 bytes long");
        }
        this.key = new SecretKeySpec(keyBytes, "HmacSHA256");
        this.expirationMs = expirationMs;
        this.users = users;
    }

    public String createToken(String email, Integer tagNumber, String nickname, String role, String clientIp) {
        Date now = new Date();
        Date validity = new Date(now.getTime() + expirationMs);

        return Jwts.builder()
                .subject(email)
                .claim("userId", accountId(email))
                .claim("tagNumber", tagNumber)
                .claim("nickname", nickname)
                .claim("role", role)
                .claim("clientIp", clientIp)
                .issuedAt(now)
                .expiration(validity)
                .signWith(key, Jwts.SIG.HS256)
                .compact();
    }

    /** Application identity after a server-verified ORCID exchange, including accounts without public email. */
    public String createOrcidToken(com.endpoint.frelog.domain.user.entity.User user,String ip,String mfaVersion) {
        if(user.getUserId()==null||user.getOrcid()==null||user.getOrcidIssuer()==null)throw new IllegalArgumentException("ORCID account required");
        Date now=new Date();
        return Jwts.builder().subject("irade-user:"+user.getUserId()).claim("userId",user.getUserId()).claim("authType","orcid")
            .claim("orcid",user.getOrcid()).claim("orcidIssuer",user.getOrcidIssuer()).claim("tagNumber",user.getTagNumber()).claim("nickname",user.getNickname())
            .claim("role",user.getRole().name()).claim("clientIp",ip).claim("mfa_version",mfaVersion).issuedAt(now).expiration(new Date(now.getTime()+expirationMs)).signWith(key,Jwts.SIG.HS256).compact();
    }
    public boolean isOrcidToken(String token){return "orcid".equals(getClaims(token).get("authType",String.class));}
    public boolean matchesOrcidIdentity(String token,com.endpoint.frelog.domain.user.entity.User user){
        var claims=getClaims(token);return user.getOrcid()!=null&&user.getOrcidIssuer()!=null&&("irade-user:"+user.getUserId()).equals(claims.getSubject())
            &&user.getOrcid().equals(claims.get("orcid",String.class))&&user.getOrcidIssuer().equals(claims.get("orcidIssuer",String.class));
    }

    public String createMfaToken(String email,Integer tag,String name,String role,String ip,String version) {
        Date now=new Date();return Jwts.builder().subject(email).claim("userId",accountId(email)).claim("tagNumber",tag).claim("nickname",name).claim("role",role).claim("clientIp",ip).claim("mfa_version",version).issuedAt(now).expiration(new Date(now.getTime()+expirationMs)).signWith(key,Jwts.SIG.HS256).compact();
    }
    private Long accountId(String email){return users==null?null:users.findByEmail(email).map(com.endpoint.frelog.domain.user.entity.User::getUserId).orElse(null);}
    public Long getUserIdFromToken(String token){Object value=getClaims(token).get("userId");return value instanceof Number n?n.longValue():null;}
    public String getMfaVersion(String token) { return getClaims(token).get("mfa_version",String.class); }
    public Date getIssuedAt(String token) { return getClaims(token).getIssuedAt(); }

    public String createCanvasAccessToken(String nickname, Integer tagNumber, Integer canvasId,
                                          String clientIp, String serverHash, long settingsRevision) {
        return createCanvasAccessToken(nickname,tagNumber,canvasId,clientIp,serverHash,settingsRevision,expirationMs);
    }
    public String createCanvasAccessToken(String nickname,Integer tagNumber,Integer canvasId,String clientIp,String serverHash,long settingsRevision,long lifetimeMs) {
        return createAccessToken(DocumentScope.CANVAS,nickname,tagNumber,canvasId,clientIp,serverHash,settingsRevision,lifetimeMs);
    }
    public String createWorkspaceAccessToken(String nickname, Integer tagNumber, Integer workspaceId,
                                             String clientIp, String serverHash, long settingsRevision) {
        return createAccessToken(DocumentScope.WORKSPACE,nickname,tagNumber,workspaceId,clientIp,serverHash,settingsRevision,expirationMs);
    }
    public String createAccessToken(DocumentScope scope,String nickname,Integer tagNumber,Integer targetId,String clientIp,String serverHash,long settingsRevision,long lifetimeMs) {
        Date now = new Date();
        Date validity = new Date(now.getTime() + lifetimeMs);

        var builder = Jwts.builder()
                .subject(scope.jwtSubject())
                .claim("nickname", nickname)
                .claim("tagNumber", tagNumber)
                .claim(scope.jwtIdClaim(), targetId)
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
        return createPasswordGrant(DocumentScope.CANVAS, userId, canvasId, settingsRevision);
    }

    public String createPasswordGrant(DocumentScope scope, Long userId, Integer targetId, long settingsRevision) {
        Date now = new Date();
        Date validity = new Date(now.getTime() + CANVAS_PASSWORD_GRANT_EXPIRATION_MS);

        return Jwts.builder()
                .subject(passwordGrantSubject(scope))
                .claim("userId", userId)
                .claim(scope.jwtIdClaim(), targetId)
                .claim("settingsRevision", settingsRevision)
                .claim("grantVersion", CANVAS_PASSWORD_GRANT_VERSION)
                .issuedAt(now)
                .expiration(validity)
                .signWith(key, Jwts.SIG.HS256)
                .compact();
    }

    /** Delegating entry to a sibling room never extends the original password proof. */
    public String createInheritedCanvasPasswordGrant(String sourceToken, Long userId, Integer canvasId, long revision) {
        Claims source=getClaims(sourceToken);
        Date now=new Date();
        Date validity=new Date(Math.min(source.getExpiration().getTime(),now.getTime()+CANVAS_PASSWORD_GRANT_EXPIRATION_MS));
        return Jwts.builder().subject("canvas-password-grant").claim("userId",userId).claim("canvasId",canvasId)
                .claim("settingsRevision",revision).claim("grantVersion",CANVAS_PASSWORD_GRANT_VERSION)
                .issuedAt(now).expiration(validity).signWith(key,Jwts.SIG.HS256).compact();
    }

    public boolean validateCanvasPasswordGrant(String token, Long userId, Integer canvasId, long settingsRevision) {
        return validatePasswordGrant(DocumentScope.CANVAS, token, userId, canvasId, settingsRevision);
    }

    public boolean validatePasswordGrant(DocumentScope scope, String token, Long userId, Integer targetId, long settingsRevision) {
        if (token == null || token.isBlank()) return false;

        try {
            Claims claims = Jwts.parser()
                    .verifyWith(key)
                    .build()
                    .parseSignedClaims(token)
                    .getPayload();
            Object tokenUserId = claims.get("userId");
            Object tokenTargetId = claims.get(scope.jwtIdClaim());
            Object tokenSettingsRevision = claims.get("settingsRevision");
            Object tokenGrantVersion = claims.get("grantVersion");
            return passwordGrantSubject(scope).equals(claims.getSubject())
                    && tokenUserId instanceof Number userIdNumber && userIdNumber.longValue() == userId
                    && tokenTargetId instanceof Number targetIdNumber && targetIdNumber.intValue() == targetId
                    && tokenSettingsRevision instanceof Number revisionNumber
                    && revisionNumber.longValue() == settingsRevision
                    && tokenGrantVersion instanceof Number grantVersionNumber
                    && grantVersionNumber.longValue() == CANVAS_PASSWORD_GRANT_VERSION;
        } catch (JwtException | IllegalArgumentException e) {
            return false;
        }
    }

    public long getCanvasPasswordGrantRemainingMs(String token) {
        return getPasswordGrantRemainingMs(DocumentScope.CANVAS, token);
    }

    public long getPasswordGrantRemainingMs(DocumentScope scope, String token) {
        Claims claims = getClaims(token);
        Object version = claims.get("grantVersion");
        if (!passwordGrantSubject(scope).equals(claims.getSubject())
                || !(version instanceof Number number)
                || number.longValue() != CANVAS_PASSWORD_GRANT_VERSION) {
            throw new IllegalArgumentException("Invalid " + scope.wireName() + " password grant");
        }
        long remaining = claims.getExpiration().getTime() - System.currentTimeMillis();
        if (remaining <= 0) throw new IllegalArgumentException("Expired " + scope.wireName() + " password grant");
        return remaining;
    }

    private static String passwordGrantSubject(DocumentScope scope) {
        return scope == DocumentScope.WORKSPACE ? "workspace-password-grant" : "canvas-password-grant";
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
