package com.endpoint.frelog.global.security;

import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.mock.web.MockHttpSession;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;
import static org.assertj.core.api.Assertions.*;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@SpringBootTest(properties={"app.oauth.google.enabled=true", "app.oauth.google.client-id=test.apps.googleusercontent.com", "app.oauth.google.client-secret=test-secret", "app.oauth.google.redirect-uri=https://frelog.p-e.kr/api/auth/oauth2/callback/google"})
class GoogleOAuthIntegrationTest {
    @MockitoBean com.endpoint.frelog.domain.auth.service.AuthService auth;
    @MockitoBean CppServerClient cpp;
    @MockitoBean CanvasRedisDocumentReader redis;
    @TestBean(methodName="database") DataSource dataSource;
    static DataSource database() {var db=new HikariDataSource();db.setJdbcUrl("jdbc:h2:mem:google_auth;MODE=MSSQLServer;DB_CLOSE_DELAY=-1");db.setUsername("sa");return db;}
    @Autowired WebApplicationContext context;
    MockMvc mvc;
    @BeforeEach void setup(){mvc=MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();}
    @Test void authorizationUsesStateNoncePkceAndFixedCallback() throws Exception {
        var result=mvc.perform(get("/api/auth/oauth2/authorize/google").secure(true).header("Host","untrusted.example")).andExpect(status().is3xxRedirection()).andReturn();
        String location=result.getResponse().getHeader("Location");
        assertThat(location).startsWith("https://accounts.google.com/").contains("state=","nonce=","code_challenge=","code_challenge_method=S256","frelog.p-e.kr").doesNotContain("test-secret", "untrusted.example");
        var session=(MockHttpSession)result.getRequest().getSession(false);
        mvc.perform(get("/api/auth/oauth2/callback/google").secure(true).session(session).param("state","wrong").param("code","invalid"))
                .andExpect(redirectedUrl("https://frelog.p-e.kr/login?oauth=failed"));
        assertThat(session.isInvalid()).isTrue();
    }
    @Test void successAndAccountConflictIgnoreInternalPortAndSpoofedHost() throws Exception {
        var chain = context.getBean("googleOAuthChain", org.springframework.security.web.SecurityFilterChain.class);
        var filter = chain.getFilters().stream().filter(f -> f instanceof org.springframework.security.oauth2.client.web.OAuth2LoginAuthenticationFilter).findFirst().orElseThrow();
        var handler = (org.springframework.security.web.authentication.AuthenticationSuccessHandler)org.springframework.test.util.ReflectionTestUtils.getField(filter, "successHandler");
        var principal = org.mockito.Mockito.mock(org.springframework.security.oauth2.core.oidc.user.OidcUser.class);
        var authentication = new org.springframework.security.oauth2.client.authentication.OAuth2AuthenticationToken(principal, java.util.List.of(), "google");
        var request = new org.springframework.mock.web.MockHttpServletRequest();request.setScheme("https");request.setServerName("internal.example");request.setServerPort(8444);
        request.addHeader("X-Forwarded-Host", "attacker.example");request.addHeader("X-Forwarded-Port", "8444");
        org.mockito.Mockito.when(auth.loginGoogle(principal, request)).thenReturn(LoginResponse.of("jwt",null));
        var response = new org.springframework.mock.web.MockHttpServletResponse();
        handler.onAuthenticationSuccess(request,response,authentication);
        assertThat(response.getRedirectedUrl()).isEqualTo("https://frelog.p-e.kr/login?oauth=success");
        assertThat(request.getSession(false).getAttribute(GoogleOAuthConfig.HANDOFF)).isInstanceOf(LoginResponse.class);
        org.mockito.Mockito.when(auth.loginGoogle(principal, request)).thenThrow(new com.endpoint.frelog.global.exception.CustomException(com.endpoint.frelog.global.exception.ErrorCode.EMAIL_ALREADY_EXISTS));
        response = new org.springframework.mock.web.MockHttpServletResponse();handler.onAuthenticationSuccess(request,response,authentication);
        assertThat(response.getRedirectedUrl()).isEqualTo("https://frelog.p-e.kr/login?oauth=account_exists");
        assertThat(request.getSession(false)).isNull();
    }

    @Test void tokenHandoffIsOneUseAndNeverCached() throws Exception {
        mvc.perform(get("/api/auth/oauth2/session")).andExpect(status().isUnauthorized());
        var session=new MockHttpSession();session.setAttribute(GoogleOAuthConfig.HANDOFF,LoginResponse.of("frelog-token",null));
        mvc.perform(get("/api/auth/oauth2/session").session(session)).andExpect(status().isOk())
                .andExpect(header().string("Cache-Control","no-store")).andExpect(jsonPath("$.accessToken").value("frelog-token"));
        assertThat(session.isInvalid()).isTrue();
        mvc.perform(get("/api/auth/oauth2/session")).andExpect(status().isUnauthorized());
    }
    @Test void providerIsDiscoverableAndApiStillRequiresJwt() throws Exception {
        mvc.perform(get("/api/auth/providers")).andExpect(status().isOk()).andExpect(jsonPath("$.google").value(true));
        mvc.perform(get("/api/canvases")).andExpect(status().isUnauthorized());
    }
}
