package com.endpoint.frelog.global.security;

import jakarta.servlet.FilterChain;
import org.junit.jupiter.api.Test;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class JwtAuthenticationFilterTest {
    static com.endpoint.frelog.domain.auth.session.AuthSessionService activeSessions() {
        var sessions = mock(com.endpoint.frelog.domain.auth.session.AuthSessionService.class);
        when(sessions.isActive(any(), any())).thenReturn(true);
        return sessions;
    }

    static com.endpoint.frelog.domain.user.entity.User user(long id) {
        var user = new com.endpoint.frelog.domain.user.entity.User("test@example.invalid", null, "Tester", 1, com.endpoint.frelog.domain.user.entity.Role.ROLE_USER);
        user.setUserId(id);
        return user;
    }

    static MockHttpServletResponse run(JwtTokenProvider tokens, CustomUserDetailsService users, com.endpoint.frelog.domain.auth.session.AuthSessionService sessions, FilterChain chain) throws Exception {
        var mfa = mock(com.endpoint.frelog.domain.auth.mfa.MfaService.class);
        when(mfa.accepts(any(), any())).thenReturn(true);
        var request = new MockHttpServletRequest("GET", "/api/auth/me");
        request.addHeader("Authorization", "Bearer session-token");
        request.setRemoteAddr("192.0.2.20");
        var response = new MockHttpServletResponse();
        try {
            new JwtAuthenticationFilter(tokens, users, mfa, null, sessions).doFilter(request, response, chain);
        } finally {
            org.springframework.security.core.context.SecurityContextHolder.clearContext();
        }
        return response;
    }

    static JwtTokenProvider sessionToken(CustomUserDetailsService users, com.endpoint.frelog.domain.user.entity.User user, java.util.Date issuedAt) {
        var tokens = mock(JwtTokenProvider.class);
        when(tokens.validateToken("session-token")).thenReturn(true);
        when(tokens.isUserAccessToken("session-token")).thenReturn(true);
        when(tokens.getEmailFromToken("session-token")).thenReturn(user.getEmail());
        when(tokens.getClientIpFromToken("session-token")).thenReturn("192.0.2.20");
        when(tokens.getUserIdFromToken("session-token")).thenReturn(user.getUserId());
        when(tokens.getSessionId("session-token")).thenReturn("sid-1");
        when(tokens.getIssuedAt("session-token")).thenReturn(issuedAt);
        when(users.loadUserByUsername(user.getEmail())).thenReturn(new CustomUserDetails(user));
        return tokens;
    }

    @Test
    void revokedSessionIsRejectedEvenWithValidSignature() throws Exception {
        var users = mock(CustomUserDetailsService.class);
        var user = user(5L);
        var tokens = sessionToken(users, user, new java.util.Date());
        var sessions = mock(com.endpoint.frelog.domain.auth.session.AuthSessionService.class);
        when(sessions.isActive("sid-1", 5L)).thenReturn(false);
        var chain = mock(FilterChain.class);
        var response = run(tokens, users, sessions, chain);
        assertEquals(401, response.getStatus());
        assertTrue(response.getContentAsString().contains("SESSION_REVOKED"));
        verifyNoInteractions(chain);
    }

    @Test
    void tokenIssuedBeforePasswordChangeIsRejected() throws Exception {
        var users = mock(CustomUserDetailsService.class);
        var user = user(5L);
        user.changePassword("new-hash");
        var tokens = sessionToken(users, user, new java.util.Date(System.currentTimeMillis() - 60_000));
        var chain = mock(FilterChain.class);
        var response = run(tokens, users, activeSessions(), chain);
        assertEquals(401, response.getStatus());
        assertTrue(response.getContentAsString().contains("PASSWORD_CHANGED"));
        verifyNoInteractions(chain);
    }

    @Test
    void activeSessionTokenAuthenticates() throws Exception {
        var users = mock(CustomUserDetailsService.class);
        var tokens = sessionToken(users, user(5L), new java.util.Date());
        var chain = mock(FilterChain.class);
        var response = run(tokens, users, activeSessions(), chain);
        assertEquals(200, response.getStatus());
        verify(chain).doFilter(any(), any());
    }

    @Test
    void canvasAccessTokenCannotAuthenticateApiRequests() throws Exception {
        var tokens = mock(JwtTokenProvider.class);
        var users = mock(CustomUserDetailsService.class);
        when(tokens.validateToken("session-token")).thenReturn(true);
        when(tokens.isUserAccessToken("session-token")).thenReturn(false);
        var chain = mock(FilterChain.class);
        var response = run(tokens, users, activeSessions(), chain);
        assertEquals(401, response.getStatus());
        assertTrue(response.getContentAsString().contains("TOKEN_TYPE"));
        verifyNoInteractions(users, chain);
    }

    @Test
    void changedNetworkReturnsStructuredReauthenticationReason() throws Exception {
        JwtTokenProvider tokens = mock(JwtTokenProvider.class);
        CustomUserDetailsService users = mock(CustomUserDetailsService.class);
        when(tokens.validateToken("fixture")).thenReturn(true);
        when(tokens.isUserAccessToken("fixture")).thenReturn(true);
        when(tokens.getEmailFromToken("fixture")).thenReturn("test@example.invalid");
        when(tokens.getClientIpFromToken("fixture")).thenReturn("192.0.2.10");
        MockHttpServletRequest request = new MockHttpServletRequest("POST", "/api/auth/me");
        request.addHeader("Authorization", "Bearer fixture");
        request.setRemoteAddr("192.0.2.20");
        MockHttpServletResponse response = new MockHttpServletResponse();
        FilterChain chain = mock(FilterChain.class);
        new JwtAuthenticationFilter(tokens, users, mock(com.endpoint.frelog.domain.auth.mfa.MfaService.class), null, activeSessions()).doFilter(request, response, chain);
        assertEquals(401, response.getStatus());
        assertTrue(response.getContentType().startsWith("application/json"));
        assertTrue(response.getContentAsString().contains("CLIENT_IP_CHANGED"));
        assertTrue(response.getContentAsString().contains("다시 로그인"));
        verifyNoInteractions(users, chain);
    }

    @Test
    void tokenBoundToCurrentNetworkContinuesAuthentication() throws Exception {
        JwtTokenProvider tokens = mock(JwtTokenProvider.class);
        CustomUserDetailsService users = mock(CustomUserDetailsService.class);
        when(tokens.validateToken("fresh")).thenReturn(true);
        when(tokens.isUserAccessToken("fresh")).thenReturn(true);
        when(tokens.getEmailFromToken("fresh")).thenReturn("test@example.invalid");
        when(tokens.getClientIpFromToken("fresh")).thenReturn("192.0.2.20");
        when(users.loadUserByUsername("test@example.invalid")).thenReturn(
                org.springframework.security.core.userdetails.User.withUsername("test@example.invalid")
                        .password("unused").roles("USER").build());
        MockHttpServletRequest request = new MockHttpServletRequest("POST", "/api/auth/me");
        request.addHeader("Authorization", "Bearer fresh");
        request.setRemoteAddr("192.0.2.20");
        MockHttpServletResponse response = new MockHttpServletResponse();
        FilterChain chain = mock(FilterChain.class);
        try {
            new JwtAuthenticationFilter(tokens, users, mock(com.endpoint.frelog.domain.auth.mfa.MfaService.class), null, activeSessions()).doFilter(request, response, chain);
            assertEquals(200, response.getStatus());
            verify(chain).doFilter(request, response);
        } finally {
            org.springframework.security.core.context.SecurityContextHolder.clearContext();
        }
    }

    @Test void botAuthenticationUsesScopedIdentityAndRejectsOutOfScopeRequests() throws Exception {
        var tokens=mock(JwtTokenProvider.class);var users=mock(CustomUserDetailsService.class);var mfa=mock(com.endpoint.frelog.domain.auth.mfa.MfaService.class);
        var bots=mock(com.endpoint.frelog.domain.canvas.bot.CanvasBotService.class);
        var bot=new com.endpoint.frelog.domain.user.entity.User("bot@example.invalid",null,"[BOT] test",1,com.endpoint.frelog.domain.user.entity.Role.ROLE_USER);
        bot.setUserId(9L);bot.setOauthProvider(com.endpoint.frelog.domain.canvas.bot.CanvasBotService.PROVIDER);
        var identity=new com.endpoint.frelog.domain.canvas.bot.CanvasBotService.Identity(new CustomUserDetails(bot),12);
        String token="agora_bot_9."+"a".repeat(43);when(bots.authenticate(token)).thenReturn(identity);
        var filter=new JwtAuthenticationFilter(tokens,users,mfa,bots,activeSessions());
        var request=new MockHttpServletRequest("POST","/api/canvases/13/access");request.addHeader("Authorization","Bearer "+token);
        var response=new MockHttpServletResponse();var chain=mock(FilterChain.class);filter.doFilter(request,response,chain);
        assertEquals(403,response.getStatus());verifyNoInteractions(chain,tokens,users,mfa);
        when(bots.allowed(identity,"GET","/api/auth/me")).thenReturn(true);
        request=new MockHttpServletRequest("GET","/api/auth/me");request.addHeader("Authorization","Bearer "+token);
        filter.doFilter(request,new MockHttpServletResponse(),chain);verify(chain).doFilter(any(),any());
        assertEquals(9L,((CustomUserDetails)org.springframework.security.core.context.SecurityContextHolder.getContext().getAuthentication().getPrincipal()).getUserId());
        org.springframework.security.core.context.SecurityContextHolder.clearContext();
    }
}
