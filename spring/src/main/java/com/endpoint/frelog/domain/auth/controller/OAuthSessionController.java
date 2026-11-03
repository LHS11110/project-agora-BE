package com.endpoint.frelog.domain.auth.controller;

import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.oauth.PendingOAuthEnrollment;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.global.security.GoogleOAuthConfig;
import com.endpoint.frelog.global.exception.*;
import jakarta.servlet.http.*;
import jakarta.validation.Valid;
import jakarta.validation.constraints.*;
import org.springframework.boot.autoconfigure.condition.ConditionalOnExpression;
import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.time.Instant;

@RestController
@ConditionalOnExpression("${app.oauth.google.enabled:false} or ${app.oauth.naver.enabled:false} or ${app.oauth.kakao.enabled:false}")
public class OAuthSessionController {
    private final AuthService auth;
    private final com.endpoint.frelog.domain.auth.email.EmailVerificationService emails;
    public OAuthSessionController(AuthService auth,com.endpoint.frelog.domain.auth.email.EmailVerificationService emails){this.auth=auth;this.emails=emails;}
    @ModelAttribute public void noCache(HttpServletResponse response){response.setHeader("Cache-Control","no-store");response.setHeader("Pragma","no-cache");}
    @GetMapping("/api/auth/oauth2/session")
    public ResponseEntity<?> session(HttpServletRequest request){
        var session=request.getSession(false);
        if(session==null)return ResponseEntity.status(401).cacheControl(CacheControl.noStore()).build();
        synchronized(session){
            Object value;
            try{value=session.getAttribute(GoogleOAuthConfig.HANDOFF);session.removeAttribute(GoogleOAuthConfig.HANDOFF);
                if(!(value instanceof GoogleOAuthConfig.EnrollmentResponse))session.invalidate();
            }catch(IllegalStateException consumed){return ResponseEntity.status(401).cacheControl(CacheControl.noStore()).build();}
            if(!(value instanceof LoginResponse)&&!(value instanceof GoogleOAuthConfig.EnrollmentResponse))return ResponseEntity.status(401).cacheControl(CacheControl.noStore()).build();
            return ResponseEntity.ok().cacheControl(CacheControl.noStore()).header("Pragma","no-cache").body(value);
        }
    }
    public record Enrollment(@NotBlank @Email @Size(max=255) String email,@NotBlank @Size(max=128) String emailVerificationToken,@NotBlank @Size(max=128) String enrollmentToken){}
    @PostMapping("/api/auth/oauth2/enrollment")
    public LoginResponse enroll(@Valid @RequestBody Enrollment body,HttpServletRequest request){
        var session=request.getSession(false);if(session==null)throw expired();
        synchronized(session){
            Object value;try{value=session.getAttribute(GoogleOAuthConfig.PENDING);}catch(IllegalStateException e){throw expired();}
            if(!(value instanceof PendingOAuthEnrollment pending)||!pending.expiresAt().isAfter(Instant.now())
                    ||!pending.origin().equals(request.getHeader("Origin"))
                    ||!MessageDigest.isEqual(pending.token().getBytes(StandardCharsets.UTF_8),body.enrollmentToken().getBytes(StandardCharsets.UTF_8)))throw expired();
            var result=auth.completeOAuthEnrollment(pending.identity(),body.email(),body.emailVerificationToken(),request);
            session.invalidate();return result;
        }
    }
    public record EmailRequest(@NotBlank @Email @Size(max=255) String email,@NotBlank @Size(max=128) String enrollmentToken){}
    @PostMapping("/api/auth/oauth2/email/request")
    public ResponseEntity<?> requestEmail(@Valid @RequestBody EmailRequest body,HttpServletRequest request){
        var session=request.getSession(false);if(session==null)throw expired();
        synchronized(session){
            Object value;try{value=session.getAttribute(GoogleOAuthConfig.PENDING);}catch(IllegalStateException e){throw expired();}
            if(!(value instanceof PendingOAuthEnrollment pending)||!pending.expiresAt().isAfter(Instant.now())
                    ||!pending.origin().equals(request.getHeader("Origin"))
                    ||!MessageDigest.isEqual(pending.token().getBytes(StandardCharsets.UTF_8),body.enrollmentToken().getBytes(StandardCharsets.UTF_8)))throw expired();
            return ResponseEntity.accepted().body(emails.request(body.email(),request.getRemoteAddr()));
        }
    }
    private CustomException expired(){return new CustomException(ErrorCode.INVALID_CREDENTIALS,"간편 로그인 가입 요청이 만료되었습니다. 다시 로그인해주세요.");}
}
