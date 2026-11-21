package com.endpoint.frelog.domain.auth.passkey;

import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIfEnvironmentVariable;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import java.nio.file.*;
import java.util.concurrent.TimeUnit;
import static org.assertj.core.api.Assertions.*;

@SpringBootTest(webEnvironment=SpringBootTest.WebEnvironment.RANDOM_PORT,properties={"server.ssl.enabled=false","server.address=127.0.0.1","app.security.cors.allowed-origins=https://frelog.p-e.kr","app.mfa.encryption-key=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="})
@EnabledIfEnvironmentVariable(named="AGORA_PASSKEY_BROWSER",matches="1")
class PasskeyBrowserIntegrationTest {
    @MockitoBean CppServerClient cpp;
    @MockitoBean CanvasRedisDocumentReader redis;
    @TestBean(methodName="database") DataSource dataSource;
    static DataSource database(){var db=new HikariDataSource();db.setJdbcUrl("jdbc:h2:mem:passkey_browser;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;INIT=CREATE SCHEMA IF NOT EXISTS dbo\\;SET SCHEMA dbo");db.setUsername("sa");return db;}
    @LocalServerPort int port;
    @org.springframework.beans.factory.annotation.Autowired com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions;
    @Autowired UserRepository users;
    @Autowired JwtTokenProvider tokens;
    @Autowired org.springframework.security.crypto.password.PasswordEncoder passwords;
    @Autowired PasskeyCredentialRepository keys;
    @Test void realBrowserAuthenticatorAndDurableServerVerification() throws Exception {
        var first=users.save(new User("passkey-test@example.invalid",passwords.encode("Browser-fixture12!"),"Passkey fixture",1,Role.ROLE_USER));
        var second=users.save(new User("passkey-other@example.invalid","fixture-password-hash","Other fixture",1,Role.ROLE_USER));
        Path output=Files.createTempFile("passkey-browser-", ".log");
        var builder=new ProcessBuilder("node","src/test/browser/passkeys.mjs");
        builder.environment().put("AGORA_PASSKEY_BACKEND","http://127.0.0.1:"+port);
        builder.environment().put("AGORA_PASSKEY_TOKEN",com.endpoint.frelog.TestSessions.token(authSessions,first,"127.0.0.1"));
        builder.environment().put("AGORA_PASSKEY_OTHER_TOKEN",com.endpoint.frelog.TestSessions.token(authSessions,second,"127.0.0.1"));
        builder.redirectErrorStream(true).redirectOutput(output.toFile());
        var process=builder.start();
        if(!process.waitFor(120,TimeUnit.SECONDS)){process.destroyForcibly();throw new AssertionError("Browser test timed out: "+Files.readString(output));}
        String log=Files.readString(output);System.out.println(log);Files.deleteIfExists(output);
        assertThat(process.exitValue()).as(log).isZero();
        assertThat(keys.findByUserIdOrderByCreatedAtAsc(first.getUserId())).isEmpty();
    }
}
