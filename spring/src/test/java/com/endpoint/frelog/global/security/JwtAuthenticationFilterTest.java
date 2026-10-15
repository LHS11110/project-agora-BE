package com.endpoint.frelog.global.security;

import jakarta.servlet.FilterChain;
import org.junit.jupiter.api.Test;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class JwtAuthenticationFilterTest {
    @Test
    void changedNetworkReturnsStructuredReauthenticationReason() throws Exception {
        JwtTokenProvider tokens = mock(JwtTokenProvider.class);
        CustomUserDetailsService users = mock(CustomUserDetailsService.class);
        when(tokens.validateToken("fixture")).thenReturn(true);
        when(tokens.getEmailFromToken("fixture")).thenReturn("test@example.invalid");
        when(tokens.getClientIpFromToken("fixture")).thenReturn("192.0.2.10");
        MockHttpServletRequest request = new MockHttpServletRequest("POST", "/api/auth/me");
        request.addHeader("Authorization", "Bearer fixture");
        request.setRemoteAddr("192.0.2.20");
        MockHttpServletResponse response = new MockHttpServletResponse();
        FilterChain chain = mock(FilterChain.class);
        new JwtAuthenticationFilter(tokens, users).doFilter(request, response, chain);
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
            new JwtAuthenticationFilter(tokens, users).doFilter(request, response, chain);
            assertEquals(200, response.getStatus());
            verify(chain).doFilter(request, response);
        } finally {
            org.springframework.security.core.context.SecurityContextHolder.clearContext();
        }
    }
}
