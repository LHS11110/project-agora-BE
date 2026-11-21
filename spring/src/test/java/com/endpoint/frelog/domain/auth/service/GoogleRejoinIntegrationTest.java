package com.endpoint.frelog.domain.auth.service;

import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.security.oauth2.core.oidc.user.OidcUser;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;

@SpringBootTest
class GoogleRejoinIntegrationTest {
    @MockitoBean CppServerClient cpp;
    @MockitoBean CanvasRedisDocumentReader redis;
    @TestBean(methodName="database") DataSource dataSource;
    static DataSource database() {
        var db = new HikariDataSource();
        db.setJdbcUrl("jdbc:h2:mem:google_rejoin;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;INIT=CREATE SCHEMA IF NOT EXISTS dbo\\;SET SCHEMA dbo");
        db.setUsername("sa"); return db;
    }
    @Autowired AuthService auth;
    @Autowired UserRepository users;
    @Autowired UserSessionRepository sessions;

    @BeforeEach void enforceProductionCredentialConstraint() throws Exception {
        try (var connection = dataSource.getConnection(); var statement = connection.createStatement()) {
            statement.execute("ALTER TABLE users ADD CONSTRAINT IF NOT EXISTS CK_users_Auth CHECK (status = 'WITHDRAWN' OR password_hash IS NOT NULL OR (oauth_provider IS NOT NULL AND oauth_id IS NOT NULL))");
        }
    }

    OidcUser principal(String suffix) {
        var p = mock(OidcUser.class);
        when(p.getSubject()).thenReturn("rejoin-" + suffix);
        when(p.getEmail()).thenReturn(suffix + "@example.invalid");
        when(p.getEmailVerified()).thenReturn(true);
        when(p.getFullName()).thenReturn("Rejoin " + suffix);
        return p;
    }
    @Test void deleteAndReauthenticateCreatesSeparateAccountAndKeepsOldAccountWithdrawn() {
        var p = principal("new"); var request = new MockHttpServletRequest();
        auth.loginGoogle(p, request);
        var original = users.findByEmail(p.getEmail()).orElseThrow();
        auth.deleteUser(original.getNickname(), original.getTagNumber(), new CustomUserDetails(original));
        auth.loginGoogle(p, request);
        var replacement = users.findByEmail(p.getEmail()).orElseThrow();
        assertThat(replacement.getUserId()).isNotEqualTo(original.getUserId());
        assertThat(replacement.getStatus()).isEqualTo(UserStatus.ACTIVE);
        assertThat(replacement.getRole()).isEqualTo(Role.ROLE_USER);
        assertThat(users.findById(original.getUserId()).orElseThrow().getStatus()).isEqualTo(UserStatus.WITHDRAWN);
        assertThat(sessions.findById(replacement.getUserId())).isPresent();
    }
    @Test void legacyWithdrawalReleasesUniqueEmailBeforeInsertAndDoesNotRestoreAdmin() {
        var p = principal("legacy");
        var original = new User(p.getEmail(), null, "deleted user-legacy", 1, Role.ROLE_ADMIN);
        original.setOauthProvider("google"); original.setOauthId(p.getSubject());
        original.setStatus(UserStatus.WITHDRAWN); original = users.saveAndFlush(original);
        auth.loginGoogle(p, new MockHttpServletRequest());
        var replacement = users.findByEmail(p.getEmail()).orElseThrow();
        assertThat(replacement.getUserId()).isNotEqualTo(original.getUserId());
        assertThat(replacement.getRole()).isEqualTo(Role.ROLE_USER);
        assertThat(users.findById(original.getUserId()).orElseThrow().getOauthId()).isNull();
        auth.loginGoogle(p, new MockHttpServletRequest());
        assertThat(users.findByEmail(p.getEmail()).orElseThrow().getUserId()).isEqualTo(replacement.getUserId());
    }
}
