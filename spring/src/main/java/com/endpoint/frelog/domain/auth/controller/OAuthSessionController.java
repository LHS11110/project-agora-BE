package com.endpoint.frelog.domain.auth.controller;

import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.global.security.GoogleOAuthConfig;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@ConditionalOnProperty(name = "app.oauth.google.enabled", havingValue = "true")
public class OAuthSessionController {
    /** One-use, same-origin handoff: no FreLog token is put into a redirect URL. */
    @GetMapping("/api/auth/oauth2/session")
    public ResponseEntity<LoginResponse> session(HttpServletRequest request) {
        var session = request.getSession(false);
        if (session == null) return ResponseEntity.status(401).cacheControl(CacheControl.noStore()).build();
        synchronized (session) {
            Object value;
            try {
                value = session.getAttribute(GoogleOAuthConfig.HANDOFF);
                session.invalidate();
            } catch (IllegalStateException consumed) {
                return ResponseEntity.status(401).cacheControl(CacheControl.noStore()).build();
            }
            if (!(value instanceof LoginResponse result)) return ResponseEntity.status(401).cacheControl(CacheControl.noStore()).build();
            return ResponseEntity.ok().cacheControl(CacheControl.noStore()).header("Pragma", "no-cache").body(result);
        }
    }
}
