package com.endpoint.frelog.domain.auth.qr;
import com.endpoint.frelog.domain.auth.mfa.*;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.test.web.servlet.*;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;
import java.time.*;
import static org.assertj.core.api.Assertions.*;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;
@SpringBootTest(properties={"app.mfa.encryption-key=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","app.security.cors.allowed-origins=https://frelog.p-e.kr"})
class QrLoginIntegrationTest {
 @MockitoBean CppServerClient cpp; @MockitoBean CanvasRedisDocumentReader redis;
 @TestBean(methodName="database") DataSource dataSource;
 static DataSource database(){var d=new HikariDataSource();d.setJdbcUrl("jdbc:h2:mem:qrlogin;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;INIT=CREATE SCHEMA IF NOT EXISTS dbo\\;SET SCHEMA dbo");d.setUsername("sa");return d;}
 @TestBean(methodName="clock") Clock mfaClock;
 static Clock clock(){return new MutableClock();}
 static class MutableClock extends Clock {Instant value=Instant.now();public ZoneId getZone(){return ZoneOffset.UTC;}public Clock withZone(ZoneId z){return this;}public Instant instant(){return value;}void advance(long seconds){value=value.plusSeconds(seconds);}}
 @org.springframework.beans.factory.annotation.Autowired com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions;
 @Autowired QrLoginService service; @Autowired QrLoginRepository qr; @Autowired UserRepository users; @Autowired UserSessionRepository sessions; @Autowired MfaStateRepository states; @Autowired MfaChallengeRepository challenges; @Autowired MfaService mfa; @Autowired JwtTokenProvider tokens; @Autowired WebApplicationContext context;
 User user;String jwt;MockHttpServletRequest phone,laptop;MockMvc mvc;
 @BeforeEach void setup(){qr.deleteAll();challenges.deleteAll();states.deleteAll();sessions.deleteAll();users.deleteAll();((MutableClock)mfaClock).value=Instant.now();user=users.save(new User("qr@example.invalid","unused","QR",1,Role.ROLE_USER));phone=new MockHttpServletRequest();phone.setRemoteAddr("192.0.2.10");laptop=new MockHttpServletRequest();laptop.setRemoteAddr("198.51.100.20");jwt=com.endpoint.frelog.TestSessions.token(authSessions,user,phone.getRemoteAddr());mvc=MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();}
 String approval(QrLoginService.Created c){return c.response().qrUrl().split("&approval=")[1];}
 QrLoginService.Poll poll(QrLoginService.Created c){return service.poll(c.response().requestId(),c.response().pollingToken(),c.browserToken(),laptop);}
 void approve(QrLoginService.Created c,String token){service.decide(c.response().requestId(),approval(c),c.response().displayCode(),true,user.getUserId(),token);}
 @Test void freshDeviceTokenOnlyAfterExplicitApprovalAndOnlyOnce(){var c=service.start(laptop);assertThat(c.response().qrUrl()).doesNotContain(jwt,c.response().pollingToken(),c.browserToken());assertThat(poll(c).login()).isNull();approve(c,jwt);((MutableClock)mfaClock).advance(2);var result=poll(c);assertThat(result.status()).isEqualTo("COMPLETE");assertThat(result.login().accessToken()).isNotEqualTo(jwt);assertThat(tokens.getClientIpFromToken(result.login().accessToken())).isEqualTo(laptop.getRemoteAddr());assertThat(tokens.getUserIdFromToken(result.login().accessToken())).isEqualTo(user.getUserId());((MutableClock)mfaClock).advance(2);assertThat(poll(c).login()).isNull();assertThat(qr.findById(c.response().requestId()).orElseThrow().userId).isNull();}
 @Test void browserCookiePollingSecretAndIpMustAllMatch(){var c=service.start(laptop);assertThatThrownBy(()->service.poll(c.response().requestId(),c.response().pollingToken(),null,laptop)).isInstanceOf(QrRejected.class);assertThatThrownBy(()->service.poll(c.response().requestId(),approval(c),c.browserToken(),laptop)).isInstanceOf(QrRejected.class);assertThatThrownBy(()->service.poll(c.response().requestId(),c.response().pollingToken(),c.browserToken(),phone)).isInstanceOf(QrRejected.class);assertThatThrownBy(()->service.preview(c.response().requestId(),c.response().pollingToken())).isInstanceOf(QrRejected.class);}
 @Test void wrongCodesPersistAndLockAfterFiveAttempts(){var c=service.start(laptop);String wrong=c.response().displayCode().equals("000000")?"111111":"000000";for(int i=0;i<5;i++)assertThatThrownBy(()->service.decide(c.response().requestId(),approval(c),wrong,true,user.getUserId(),jwt)).isInstanceOf(QrRejected.class);assertThat(poll(c).status()).isEqualTo("DENIED");assertThatThrownBy(()->approve(c,jwt)).isInstanceOf(QrRejected.class);}
 @Test void expiryCancellationDenialAndStartLimit(){var c=service.start(laptop);service.cancel(c.response().requestId(),c.response().pollingToken(),c.browserToken(),laptop);assertThat(poll(c).status()).isEqualTo("CANCELLED");var denied=service.start(laptop);service.decide(denied.response().requestId(),approval(denied),null,false,user.getUserId(),jwt);assertThat(poll(denied).status()).isEqualTo("DENIED");var expired=service.start(laptop);((MutableClock)mfaClock).advance(181);assertThatThrownBy(()->poll(expired)).isInstanceOf(QrRejected.class);for(int i=0;i<6;i++)service.start(laptop);assertThatThrownBy(()->service.start(laptop)).isInstanceOf(QrRejected.class);}
 @Test void originsAndApprovalEndpointsAreProtected() throws Exception {mvc.perform(post("/api/auth/qr/start").header("Origin","https://evil.invalid")).andExpect(status().isForbidden());mvc.perform(post("/api/auth/qr/start").header("Origin","https://frelog.p-e.kr")).andExpect(status().isOk()).andExpect(header().string("Cache-Control","no-store")).andExpect(header().string("Set-Cookie",org.hamcrest.Matchers.containsString("HttpOnly")));for(String path:new String[]{"preview","decide"})mvc.perform(post("/api/auth/qr/"+path).contentType("application/json").content("{}")).andExpect(status().isUnauthorized());}
 @Test void mfaRevisionChangeInvalidatesPendingApproval(){String secret=mfa.begin(user.getUserId(),jwt).get("secret");String code=Totp.code(secret,mfaClock.instant().getEpochSecond()/30);var enrollment=mfa.confirm(user.getUserId(),code,jwt,phone);var c=service.start(laptop);assertThatThrownBy(()->approve(c,jwt)).isInstanceOf(QrRejected.class);approve(c,enrollment.login().accessToken());mfa.disable(user.getUserId(),enrollment.recoveryCodes().get(0));secret=mfa.begin(user.getUserId(),enrollment.login().accessToken()).get("secret");mfa.confirm(user.getUserId(),Totp.code(secret,mfaClock.instant().getEpochSecond()/30),enrollment.login().accessToken(),phone);assertThatThrownBy(()->poll(c)).isInstanceOf(MfaRejected.class);}
}
