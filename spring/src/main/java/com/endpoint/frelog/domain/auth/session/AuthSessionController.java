package com.endpoint.frelog.domain.auth.session;

import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.auth.mfa.MfaService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;

import java.util.Map;

/**
 * Cookie-authenticated session endpoints. The refresh cookie is SameSite=Strict and scoped to
 * this path; the custom header additionally forces a CORS preflight for cross-origin callers.
 */
@RestController
@RequestMapping(AuthSessionService.COOKIE_PATH)
public class AuthSessionController {
    public static final String HEADER = "X-Agora-Session";

    private final AuthSessionService sessions;
    private final JwtTokenProvider tokens;
    private final MfaService mfa;

    public AuthSessionController(AuthSessionService sessions, JwtTokenProvider tokens, MfaService mfa) {
        this.sessions=sessions;this.tokens=tokens;this.mfa=mfa;
    }

    private static ResponseEntity<Object> rejected(String reason) {
        return ResponseEntity.status(401).cacheControl(CacheControl.noStore())
                .body(Map.of("status",401,"code","AUTH_004","reason",reason,"message","로그인 세션이 만료되었습니다. 다시 로그인해주세요."));
    }
    private static boolean fromApp(HttpServletRequest request){return "1".equals(request.getHeader(HEADER));}

    @PostMapping("/refresh")
    public ResponseEntity<Object> refresh(HttpServletRequest request, HttpServletResponse response) {
        if(!fromApp(request))return rejected("MISSING_SESSION_HEADER");
        try{
            var refreshed=sessions.refresh(request,response);
            Long userId=refreshed.user().getUserId();
            if(!mfa.accepts(userId,refreshed.accessToken())){
                sessions.logout(tokens.getSessionId(refreshed.accessToken()),request,response);
                return ResponseEntity.status(401).cacheControl(CacheControl.noStore()).body(Map.of("code","MFA_REQUIRED","message","MFA 인증을 위해 다시 로그인해주세요."));
            }
            return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(LoginResponse.of(refreshed.accessToken(),UserResponse.from(refreshed.user())));
        }catch(AuthSessionService.Rejected rejected){return rejected(rejected.reason());}
    }

    @PostMapping("/logout")
    public ResponseEntity<Void> logout(HttpServletRequest request, HttpServletResponse response) {
        if(!fromApp(request))return ResponseEntity.status(400).build();
        sessions.logout(sessions.currentSessionId(),request,response);
        return ResponseEntity.noContent().cacheControl(CacheControl.noStore()).build();
    }

    /** Signs out every device, including this one. */
    @PostMapping("/logout-all")
    public ResponseEntity<Void> logoutAll(@AuthenticationPrincipal CustomUserDetails user, HttpServletRequest request, HttpServletResponse response) {
        if(user==null)return ResponseEntity.status(401).build();
        if(!fromApp(request))return ResponseEntity.status(400).build();
        sessions.revokeAll(user.getUserId(),null);
        AuthSessionService.clearCookie(response);
        return ResponseEntity.noContent().cacheControl(CacheControl.noStore()).build();
    }
}
