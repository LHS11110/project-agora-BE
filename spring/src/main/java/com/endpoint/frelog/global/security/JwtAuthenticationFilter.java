package com.endpoint.frelog.global.security;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.security.core.userdetails.UserDetails;
import org.springframework.security.core.userdetails.UsernameNotFoundException;
import org.springframework.security.web.authentication.WebAuthenticationDetailsSource;
import org.springframework.stereotype.Component;
import org.springframework.util.StringUtils;
import org.springframework.web.filter.OncePerRequestFilter;
import org.springframework.http.MediaType;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.logging.RequestLogAttributes;

import java.io.IOException;

@Component
public class JwtAuthenticationFilter extends OncePerRequestFilter {

    private final com.endpoint.frelog.domain.auth.mfa.MfaService mfa;
    private final JwtTokenProvider tokenProvider;
    private final CustomUserDetailsService userDetailsService;
    private final com.endpoint.frelog.domain.canvas.bot.CanvasBotService bots;

    public JwtAuthenticationFilter(JwtTokenProvider tokenProvider, CustomUserDetailsService userDetailsService, com.endpoint.frelog.domain.auth.mfa.MfaService mfa) {
        this(tokenProvider,userDetailsService,mfa,null);
    }
    @org.springframework.beans.factory.annotation.Autowired
    public JwtAuthenticationFilter(JwtTokenProvider tokenProvider, CustomUserDetailsService userDetailsService, com.endpoint.frelog.domain.auth.mfa.MfaService mfa, com.endpoint.frelog.domain.canvas.bot.CanvasBotService bots) {
        this.bots=bots;
        this.mfa = mfa;
        this.tokenProvider = tokenProvider;
        this.userDetailsService = userDetailsService;
    }

    @Override
    protected void doFilterInternal(HttpServletRequest request,
                                    HttpServletResponse response,
                                    FilterChain filterChain) throws ServletException, IOException {
        String token = resolveToken(request);

        if (token != null && token.startsWith(com.endpoint.frelog.domain.canvas.bot.CanvasBotService.PREFIX)) {
            var identity = bots == null ? null : bots.authenticate(token);
            if (identity == null || !bots.allowed(identity, request.getMethod(), request.getRequestURI())) {
                SecurityContextHolder.clearContext();response.setStatus(identity==null?401:403);response.setContentType(MediaType.APPLICATION_JSON_VALUE);response.setCharacterEncoding("UTF-8");
                response.getWriter().write("{\"code\":\"BOT_ACCESS_DENIED\",\"message\":\"봇 인증 또는 캔버스 접근 권한을 확인하세요.\"}");return;
            }
            var auth = new UsernamePasswordAuthenticationToken(identity.user(),null,identity.user().getAuthorities());
            auth.setDetails(new WebAuthenticationDetailsSource().buildDetails(request));SecurityContextHolder.getContext().setAuthentication(auth);
            filterChain.doFilter(request,response);return;
        }
        if (StringUtils.hasText(token) && tokenProvider.validateToken(token)) {
            String email = tokenProvider.getEmailFromToken(token);
            String tokenIp = tokenProvider.getClientIpFromToken(token);
            String currentIp = request.getRemoteAddr();

            if (tokenIp != null && !tokenIp.equals(currentIp)) {
                RequestLogAttributes.markFailure(request, ErrorCode.INVALID_TOKEN.getCode(),
                        new SecurityException("Token client IP mismatch"));
                response.setStatus(HttpServletResponse.SC_UNAUTHORIZED);
                response.setContentType(MediaType.APPLICATION_JSON_VALUE);
                response.setCharacterEncoding("UTF-8");
                response.getWriter().write(
                        "{\"status\":401,\"error\":\"UNAUTHORIZED\",\"code\":\"AUTH_004\",\"reason\":\"CLIENT_IP_CHANGED\",\"message\":\"접속 네트워크가 변경되었습니다. 다시 로그인해주세요.\"}"
                );
                return;
            }

            UserDetails userDetails;
            try {
                userDetails = userDetailsService.loadUserByUsername(email);
            } catch (UsernameNotFoundException ex) {
                // A signed token can outlive the account it refers to, for example
                // after a database reset. Treat it as stale authentication instead
                // of allowing the lookup exception to become an HTTP 500.
                SecurityContextHolder.clearContext();
                RequestLogAttributes.markFailure(request, ErrorCode.INVALID_TOKEN.getCode(), ex);
                response.setStatus(HttpServletResponse.SC_UNAUTHORIZED);
                response.setContentType(MediaType.APPLICATION_JSON_VALUE);
                response.setCharacterEncoding("UTF-8");
                response.getWriter().write(
                        "{\"status\":401,\"error\":\"UNAUTHORIZED\",\"code\":\"AUTH_004\",\"message\":\"유효하지 않거나 만료된 인증 토큰입니다.\"}"
                );
                return;
            }

            if(userDetails instanceof CustomUserDetails current && com.endpoint.frelog.domain.canvas.bot.CanvasBotService.isBot(current.getUser())) {
                response.setStatus(401);return;
            }
            if(userDetails instanceof CustomUserDetails current&&!java.util.Objects.equals(current.getUserId(),tokenProvider.getUserIdFromToken(token))){
                SecurityContextHolder.clearContext();response.setStatus(401);response.setContentType(MediaType.APPLICATION_JSON_VALUE);response.setCharacterEncoding("UTF-8");
                response.getWriter().write("{\"code\":\"AUTH_004\",\"message\":\"유효하지 않거나 만료된 인증 토큰입니다. 다시 로그인해주세요.\"}");return;
            }
            if (userDetails instanceof CustomUserDetails current && !mfa.accepts(current.getUserId(), token)) {
                response.setStatus(401);response.setContentType(MediaType.APPLICATION_JSON_VALUE);response.setCharacterEncoding("UTF-8");response.getWriter().write("{\"code\":\"MFA_REQUIRED\",\"message\":\"MFA 인증을 위해 다시 로그인해주세요.\"}");return;
            }
            if (userDetails.isEnabled() && userDetails.isAccountNonLocked()) {
                UsernamePasswordAuthenticationToken authentication =
                        new UsernamePasswordAuthenticationToken(userDetails, null, userDetails.getAuthorities());
                authentication.setDetails(new WebAuthenticationDetailsSource().buildDetails(request));
                SecurityContextHolder.getContext().setAuthentication(authentication);
            }
        }

        filterChain.doFilter(request, response);
    }

    private String resolveToken(HttpServletRequest request) {
        String bearerToken = request.getHeader("Authorization");
        if (StringUtils.hasText(bearerToken) && bearerToken.startsWith("Bearer ")) {
            return bearerToken.substring(7);
        }
        return null;
    }
}
