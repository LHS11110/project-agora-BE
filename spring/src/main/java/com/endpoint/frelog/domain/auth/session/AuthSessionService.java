package com.endpoint.frelog.domain.auth.session;

import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import jakarta.servlet.http.Cookie;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpHeaders;
import org.springframework.http.ResponseCookie;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.context.request.RequestContextHolder;
import org.springframework.web.context.request.ServletRequestAttributes;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.time.Clock;
import java.time.Duration;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.Base64;
import java.util.HexFormat;
import java.util.UUID;

/**
 * Server-side login sessions behind short-lived access tokens.
 * Access JWTs carry the session id, so logout and revocation take effect on the next request.
 */
@Service
public class AuthSessionService {
    public static final String COOKIE = "__Secure-agora_refresh";
    public static final String COOKIE_PATH = "/api/auth/session";
    /** Concurrent refreshes from tabs racing the same rotation are not treated as token theft. */
    private static final Duration ROTATION_GRACE = Duration.ofSeconds(30);

    public static final class Rejected extends RuntimeException {
        private final String reason;
        public Rejected(String reason){super(reason);this.reason=reason;}
        public String reason(){return reason;}
    }
    public record Refreshed(String accessToken, User user) {}

    private final AuthSessionRepository sessions;
    private final UserRepository users;
    private final JwtTokenProvider tokens;
    private final Clock clock;
    private final Duration idleLifetime;
    private final Duration maxLifetime;
    private final SecureRandom random = new SecureRandom();

    public AuthSessionService(AuthSessionRepository sessions, UserRepository users, JwtTokenProvider tokens, Clock clock,
                              @Value("${jwt.refresh-idle-ms:1209600000}") long idleMs,
                              @Value("${jwt.refresh-max-ms:2592000000}") long maxMs) {
        this.sessions=sessions;this.users=users;this.tokens=tokens;this.clock=clock;
        this.idleLifetime=Duration.ofMillis(idleMs);this.maxLifetime=Duration.ofMillis(maxMs);
    }

    private LocalDateTime now(){return LocalDateTime.ofInstant(clock.instant(),ZoneOffset.UTC);}
    private String secret(){byte[] bytes=new byte[32];random.nextBytes(bytes);return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);}
    private static String hash(String value){
        try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8)));}
        catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}
    }
    private LocalDateTime nextExpiry(AuthSession s,LocalDateTime now){
        LocalDateTime idle=now.plus(idleLifetime),cap=s.createdAt.plus(maxLifetime);return idle.isBefore(cap)?idle:cap;
    }

    /** Starts a session for a fully authenticated login and returns its first access token. */
    @Transactional
    public String issue(User user, HttpServletRequest request, String mfaVersion) {
        var now=now();var s=new AuthSession();String raw=secret();
        s.id=UUID.randomUUID().toString();s.userId=user.getUserId();s.refreshHash=hash(raw);s.clientIp=request.getRemoteAddr();
        s.mfaVersion=mfaVersion;s.createdAt=now;s.expiresAt=nextExpiry(s,now);
        sessions.save(s);
        writeCookie(currentResponse(),raw,Duration.between(now,s.expiresAt));
        return tokens.createUserAccessToken(user,s.clientIp,mfaVersion,s.id);
    }

    /** Rotates the refresh secret. A rotated-away secret presented again revokes the session. */
    @Transactional(noRollbackFor=Rejected.class)
    public Refreshed refresh(HttpServletRequest request, HttpServletResponse response) {
        String raw=readCookie(request);
        if(raw==null)throw new Rejected("NO_SESSION");
        var now=now();String presented=hash(raw);
        var found=sessions.lockByRefreshHash(presented);
        if(found.isEmpty()){
            var replayed=sessions.lockByPreviousHash(presented).orElse(null);
            if(replayed!=null&&replayed.revokedAt==null){
                if(replayed.rotatedAt!=null&&replayed.rotatedAt.plus(ROTATION_GRACE).isAfter(now))throw new Rejected("REFRESH_RACE");
                replayed.revokedAt=now;sessions.save(replayed);
            }
            clearCookie(response);throw new Rejected("SESSION_REVOKED");
        }
        var s=found.get();
        if(s.revokedAt!=null||!s.expiresAt.isAfter(now)){clearCookie(response);throw new Rejected("SESSION_EXPIRED");}
        if(s.clientIp!=null&&!s.clientIp.equals(request.getRemoteAddr()))throw new Rejected("CLIENT_IP_CHANGED");
        User user=users.findById(s.userId).orElse(null);
        if(user==null||!user.isActive()){s.revokedAt=now;sessions.save(s);clearCookie(response);throw new Rejected("SESSION_REVOKED");}
        String next=secret();
        s.previousHash=presented;s.refreshHash=hash(next);s.rotatedAt=now;s.expiresAt=nextExpiry(s,now);
        sessions.save(s);
        writeCookie(response,next,Duration.between(now,s.expiresAt));
        return new Refreshed(tokens.createUserAccessToken(user,s.clientIp,s.mfaVersion,s.id),user);
    }

    @Transactional(readOnly=true)
    public boolean isActive(String sessionId, Long userId) {
        return sessionId!=null&&userId!=null&&sessions.isActive(sessionId,userId,now());
    }

    /** Ends the session named by the access token, or else by the refresh cookie. */
    @Transactional
    public void logout(String accessSessionId, HttpServletRequest request, HttpServletResponse response) {
        var now=now();
        if(accessSessionId!=null)sessions.findById(accessSessionId).ifPresent(s->{if(s.revokedAt==null){s.revokedAt=now;sessions.save(s);}});
        String raw=readCookie(request);
        if(raw!=null)sessions.lockByRefreshHash(hash(raw)).ifPresent(s->{if(s.revokedAt==null){s.revokedAt=now;sessions.save(s);}});
        clearCookie(response);
    }

    /** Revokes every session of the user except {@code keepSessionId} (null revokes all). */
    @Transactional
    public int revokeAll(Long userId, String keepSessionId) {
        return userId==null?0:sessions.revokeAll(userId,keepSessionId,now());
    }

    /** Session id of the access token on the current request, if any. */
    public String currentSessionId() {
        if(!(RequestContextHolder.getRequestAttributes() instanceof ServletRequestAttributes attributes))return null;
        String header=attributes.getRequest().getHeader("Authorization");
        if(header==null||!header.startsWith("Bearer "))return null;
        String token=header.substring(7);
        return tokens.validateToken(token)&&tokens.isUserAccessToken(token)?tokens.getSessionId(token):null;
    }

    @Scheduled(fixedDelay=3600000) @Transactional
    public void cleanup(){sessions.deleteStale(now().minusDays(1));}

    private static HttpServletResponse currentResponse(){
        return RequestContextHolder.getRequestAttributes() instanceof ServletRequestAttributes attributes?attributes.getResponse():null;
    }
    private static String readCookie(HttpServletRequest request){
        Cookie[] cookies=request.getCookies();if(cookies==null)return null;
        for(Cookie cookie:cookies)if(COOKIE.equals(cookie.getName())&&cookie.getValue()!=null&&cookie.getValue().matches("[A-Za-z0-9_-]{43}"))return cookie.getValue();
        return null;
    }
    private static void writeCookie(HttpServletResponse response,String value,Duration maxAge){
        if(response==null)return;
        response.addHeader(HttpHeaders.SET_COOKIE,ResponseCookie.from(COOKIE,value).httpOnly(true).secure(true).sameSite("Strict").path(COOKIE_PATH).maxAge(maxAge).build().toString());
    }
    public static void clearCookie(HttpServletResponse response){writeCookie(response,"",Duration.ZERO);}
}
