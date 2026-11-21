package com.endpoint.frelog.domain.auth.session;

import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.repository.UserSessionRepository;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.http.Cookie;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.http.MediaType;
import org.springframework.mock.web.MockHttpServletResponse;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;

import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.Arrays;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@SpringBootTest
class AuthSessionIntegrationTest {
    @MockitoBean CppServerClient cppServerClient;
    @MockitoBean CanvasRedisDocumentReader canvasRedisDocumentReader;
    @Autowired WebApplicationContext context;
    @Autowired UserRepository users;
    @Autowired UserSessionRepository userSessions;
    @Autowired AuthSessionRepository sessions;
    @Autowired PasswordEncoder passwords;
    final ObjectMapper json = new ObjectMapper();
    MockMvc mvc;
    User user;

    record Login(String token, Cookie refresh) {}

    @BeforeEach void setUp() {
        mvc = MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();
        sessions.deleteAll(); userSessions.deleteAll(); users.deleteAll();
        user = new User("session@example.invalid", passwords.encode("Session-test-12!"), "Session", 1, Role.ROLE_USER);
        user.setStatus(UserStatus.ACTIVE);
        user = users.save(user);
    }

    static Cookie refreshCookie(MockHttpServletResponse response) {
        // Last Set-Cookie wins, as in a browser.
        return Arrays.stream(response.getCookies()).filter(c -> AuthSessionService.COOKIE.equals(c.getName())).reduce((a, b) -> b).orElse(null);
    }

    Login login() throws Exception {
        var response = mvc.perform(post("/api/auth/login").contentType(MediaType.APPLICATION_JSON)
                .content("{\"email\":\"session@example.invalid\",\"password\":\"Session-test-12!\"}"))
                .andExpect(status().isOk()).andReturn().getResponse();
        var cookie = refreshCookie(response);
        assertThat(cookie).isNotNull();
        assertThat(cookie.isHttpOnly()).isTrue();
        assertThat(cookie.getSecure()).isTrue();
        assertThat(cookie.getPath()).isEqualTo(AuthSessionService.COOKIE_PATH);
        assertThat(response.getHeader("Set-Cookie")).contains("SameSite=Strict");
        return new Login(json.readTree(response.getContentAsString()).get("accessToken").asText(), cookie);
    }

    MockHttpServletResponse refresh(Cookie cookie) throws Exception {
        return mvc.perform(post("/api/auth/session/refresh").header(AuthSessionController.HEADER, "1").cookie(cookie)).andReturn().getResponse();
    }

    @Test void refreshRotatesTheCookieAndIssuesAWorkingAccessToken() throws Exception {
        var login = login();
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + login.token())).andExpect(status().isOk());

        mvc.perform(post("/api/auth/session/refresh").cookie(login.refresh())).andExpect(status().isUnauthorized())
                .andExpect(jsonPath("$.reason").value("MISSING_SESSION_HEADER"));

        var refreshed = refresh(login.refresh());
        assertThat(refreshed.getStatus()).isEqualTo(200);
        var rotated = refreshCookie(refreshed);
        assertThat(rotated.getValue()).isNotEqualTo(login.refresh().getValue());
        String next = json.readTree(refreshed.getContentAsString()).get("accessToken").asText();
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + next)).andExpect(status().isOk());
        assertThat(refresh(rotated).getStatus()).isEqualTo(200);
    }

    @Test void replayedRefreshSecretRevokesTheSessionAfterTheRaceWindow() throws Exception {
        var login = login();
        var rotated = refreshCookie(refresh(login.refresh()));

        // Another tab racing the same rotation is told to retry, without revoking anything.
        var race = refresh(login.refresh());
        assertThat(race.getStatus()).isEqualTo(401);
        assertThat(race.getContentAsString()).contains("REFRESH_RACE");

        var session = sessions.findAll().getFirst();
        session.rotatedAt = LocalDateTime.now(ZoneOffset.UTC).minusMinutes(5);
        sessions.save(session);
        var stolen = refresh(login.refresh());
        assertThat(stolen.getContentAsString()).contains("SESSION_REVOKED");
        assertThat(refresh(rotated).getStatus()).isEqualTo(401);
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + login.token())).andExpect(status().isUnauthorized())
                .andExpect(jsonPath("$.reason").value("SESSION_REVOKED"));
    }

    @Test void logoutEndsTheSessionImmediately() throws Exception {
        var login = login();
        mvc.perform(post("/api/auth/session/logout").header(AuthSessionController.HEADER, "1")
                .header("Authorization", "Bearer " + login.token()).cookie(login.refresh())).andExpect(status().isNoContent());
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + login.token())).andExpect(status().isUnauthorized());
        assertThat(refresh(login.refresh()).getStatus()).isEqualTo(401);
    }

    @Test void logoutAllEndsEverySession() throws Exception {
        var first = login();
        var second = login();
        mvc.perform(post("/api/auth/session/logout-all").header(AuthSessionController.HEADER, "1")
                .header("Authorization", "Bearer " + first.token())).andExpect(status().isNoContent());
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + second.token())).andExpect(status().isUnauthorized());
        assertThat(refresh(second.refresh()).getStatus()).isEqualTo(401);
    }

    @Test void passwordChangeSignsOutOtherDevicesAndKeepsTheCaller() throws Exception {
        var caller = login();
        var other = login();
        Thread.sleep(1100); // iat has second precision
        mvc.perform(put("/api/users/Session/1").header("Authorization", "Bearer " + caller.token())
                .contentType(MediaType.APPLICATION_JSON).content("{\"password\":\"Changed-pass-34!\"}")).andExpect(status().isOk());

        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + other.token())).andExpect(status().isUnauthorized());
        assertThat(refresh(other.refresh()).getStatus()).isEqualTo(401);

        // The caller's access token predates the change, but its session survives a refresh.
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + caller.token())).andExpect(status().isUnauthorized())
                .andExpect(jsonPath("$.reason").value("PASSWORD_CHANGED"));
        var refreshed = refresh(caller.refresh());
        assertThat(refreshed.getStatus()).isEqualTo(200);
        String next = json.readTree(refreshed.getContentAsString()).get("accessToken").asText();
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + next)).andExpect(status().isOk());
    }

    @Test void canvasAccessTokensCannotCallTheApi() throws Exception {
        var tokens = context.getBean(com.endpoint.frelog.global.security.JwtTokenProvider.class);
        String canvas = tokens.createCanvasAccessToken("Session", 1, 1, "127.0.0.1", "hash", 0L);
        mvc.perform(get("/api/auth/me").header("Authorization", "Bearer " + canvas)).andExpect(status().isUnauthorized())
                .andExpect(jsonPath("$.reason").value("TOKEN_TYPE"));
    }
}
