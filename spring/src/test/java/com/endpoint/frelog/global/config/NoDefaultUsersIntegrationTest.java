package com.endpoint.frelog.global.config;

import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.repository.UserSessionRepository;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.context.bean.override.convention.TestBean;
import static org.assertj.core.api.Assertions.assertThat;

@SpringBootTest
class NoDefaultUsersIntegrationTest {
    @MockitoBean CppServerClient cppServerClient;
    @MockitoBean CanvasRedisDocumentReader canvasRedisDocumentReader;
    // Production's fallback uses a fixed database name. Isolate this startup test.
    @TestBean(methodName = "emptyDatabase") DataSource dataSource;
    @Autowired UserRepository users;
    @Autowired UserSessionRepository sessions;

    static DataSource emptyDatabase() {
        HikariDataSource db = new HikariDataSource();
        db.setJdbcUrl("jdbc:h2:mem:no_default_users;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;DB_CLOSE_ON_EXIT=FALSE");
        db.setUsername("sa");
        return db;
    }

    @Test
    void startupWithEmptyDatabaseDoesNotCreateUsersOrSessions() {
        assertThat(users.count()).isZero();
        assertThat(sessions.count()).isZero();
    }
}
