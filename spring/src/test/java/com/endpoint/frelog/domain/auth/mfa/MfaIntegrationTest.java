package com.endpoint.frelog.domain.auth.mfa;
import com.endpoint.frelog.domain.auth.dto.*;
import com.endpoint.frelog.domain.auth.service.AuthService;
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
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.test.web.servlet.*;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;
import java.time.*;
import static org.assertj.core.api.Assertions.*;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@SpringBootTest(properties="app.mfa.encryption-key=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
class MfaIntegrationTest {
 @MockitoBean CppServerClient cpp;
 @MockitoBean CanvasRedisDocumentReader redis;
 @TestBean(methodName="database") DataSource dataSource;
 static DataSource database(){var d=new HikariDataSource();d.setJdbcUrl("jdbc:h2:mem:mfa;MODE=MSSQLServer;DB_CLOSE_DELAY=-1");d.setUsername("sa");return d;}
 @TestBean(methodName="clock") Clock mfaClock;
 static Clock clock(){return new MutableClock();}
 static class MutableClock extends Clock {Instant value=Instant.now();public ZoneId getZone(){return ZoneOffset.UTC;}public Clock withZone(ZoneId zone){return this;}public Instant instant(){return value;}void advance(long seconds){value=value.plusSeconds(seconds);}}
 @Autowired MfaService mfa;@Autowired MfaStateRepository states;@Autowired MfaChallengeRepository challenges;
 @Autowired UserRepository users;@Autowired UserSessionRepository sessions;@Autowired PasswordEncoder passwords;@Autowired JwtTokenProvider tokens;@Autowired AuthService auth;
 @Autowired WebApplicationContext context;
 User user;String original,secret;MockHttpServletRequest request;MockMvc mvc;
 @BeforeEach void setup(){challenges.deleteAll();states.deleteAll();sessions.deleteAll();users.deleteAll();((MutableClock)mfaClock).value=Instant.now();user=users.save(new User("mfa@example.invalid",passwords.encode("test-password"),"MFA",1,Role.ROLE_USER));request=new MockHttpServletRequest();request.setRemoteAddr("127.0.0.1");original=tokens.createToken(user.getEmail(),1,"MFA","ROLE_USER","127.0.0.1");mvc=MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();}
 String code(){return Totp.code(secret,mfaClock.instant().getEpochSecond()/30);}
 MfaService.Enrollment enroll(){secret=mfa.begin(user.getUserId(),original).get("secret");return mfa.confirm(user.getUserId(),code(),original,request);}
 @Test void enrollmentIsEncryptedAndOldJwtCannotBypassMfa() throws Exception {
  var enrollment=enroll();var state=states.findById(user.getUserId()).orElseThrow();assertThat(state.secret).doesNotContain(secret);assertThat(state.pendingSecret).isNull();assertThat(enrollment.recoveryCodes()).hasSize(10);
  assertThat(mfa.accepts(user.getUserId(),original)).isFalse();assertThat(mfa.accepts(user.getUserId(),enrollment.login().accessToken())).isTrue();
  mvc.perform(get("/api/auth/mfa").header("Authorization","Bearer "+original)).andExpect(status().isUnauthorized());
  mvc.perform(get("/api/auth/mfa").header("Authorization","Bearer "+enrollment.login().accessToken())).andExpect(status().isOk()).andExpect(jsonPath("$.enabled").value(true));
  var first=auth.login(new LoginRequest(user.getEmail(),"test-password"),request);assertThat(first.mfaRequired()).isTrue();assertThat(first.accessToken()).isNull();assertThat(tokens.validateToken(first.challengeToken())).isFalse();
  ((MutableClock)mfaClock).advance(60);var completed=mfa.complete(first.challengeToken(),code(),request);assertThat(completed.mfaRequired()).isFalse();assertThat(tokens.validateToken(completed.accessToken())).isTrue();
  assertThatThrownBy(()->mfa.complete(first.challengeToken(),enrollment.recoveryCodes().get(0),request)).isInstanceOf(MfaRejected.class);
  var next=mfa.challenge(user,request);assertThatThrownBy(()->mfa.complete(next.challengeToken(),code(),request)).isInstanceOf(MfaRejected.class);
 }
 @Test void recoveryCodesAreHashedSingleUseAndCanDisableMfa(){var enrolled=enroll();String recovery=enrolled.recoveryCodes().get(0);assertThat(states.findById(user.getUserId()).orElseThrow().recoveryHashes).doesNotContain(recovery);
  mfa.complete(mfa.challenge(user,request).challengeToken(),recovery,request);
  assertThatThrownBy(()->mfa.complete(mfa.challenge(user,request).challengeToken(),recovery,request)).isInstanceOf(MfaRejected.class);
  mfa.disable(user.getUserId(),enrolled.recoveryCodes().get(1));assertThat(mfa.enabled(user.getUserId())).isFalse();assertThat(states.findById(user.getUserId()).orElseThrow().secret).isNull();
 }
 @Test void incorrectAttemptsCommitAndNewChallengeCannotResetLock(){enroll();String wrong="000000";while(Totp.match(secret,wrong,mfaClock.instant().getEpochSecond()/30,-1)>=0)wrong=String.format("%06d",Integer.parseInt(wrong)+1);final String input=wrong;
  for(int i=0;i<5;i++){String challenge=mfa.challenge(user,request).challengeToken();assertThatThrownBy(()->mfa.complete(challenge,input,request)).isInstanceOf(MfaRejected.class);}
  var state=states.findById(user.getUserId()).orElseThrow();assertThat(state.failures).isEqualTo(5);assertThat(state.lockedUntil).isAfter(LocalDateTime.ofInstant(mfaClock.instant(),ZoneOffset.UTC));
  ((MutableClock)mfaClock).advance(60);assertThatThrownBy(()->mfa.complete(mfa.challenge(user,request).challengeToken(),code(),request)).isInstanceOf(MfaRejected.class);
  ((MutableClock)mfaClock).advance(301);assertThat(mfa.complete(mfa.challenge(user,request).challengeToken(),code(),request).accessToken()).isNotBlank();
 }
 @Test void pendingEnrollmentExpiryAndChallengeIpExpiryAreEnforced(){secret=mfa.begin(user.getUserId(),original).get("secret");((MutableClock)mfaClock).advance(601);assertThatThrownBy(()->mfa.confirm(user.getUserId(),code(),original,request)).isInstanceOf(MfaRejected.class);
  ((MutableClock)mfaClock).value=Instant.now();enroll();String challenge=mfa.challenge(user,request).challengeToken();var other=new MockHttpServletRequest();other.setRemoteAddr("192.0.2.1");assertThatThrownBy(()->mfa.complete(challenge,code(),other)).isInstanceOf(MfaRejected.class);((MutableClock)mfaClock).advance(301);assertThatThrownBy(()->mfa.complete(challenge,code(),request)).isInstanceOf(MfaRejected.class);
 }
 @Test void regeneratingCodesRevokesOldCodesAndReenrollmentRevokesOldMfaTokens(){
  var first=enroll();var oldToken=first.login().accessToken();var codes=mfa.regenerate(user.getUserId(),first.recoveryCodes().get(0));
  assertThatThrownBy(()->mfa.complete(mfa.challenge(user,request).challengeToken(),first.recoveryCodes().get(1),request)).isInstanceOf(MfaRejected.class);
  mfa.disable(user.getUserId(),codes.get(0));secret=mfa.begin(user.getUserId(),oldToken).get("secret");
  var second=mfa.confirm(user.getUserId(),code(),oldToken,request);
  assertThat(mfa.accepts(user.getUserId(),oldToken)).isFalse();assertThat(mfa.accepts(user.getUserId(),second.login().accessToken())).isTrue();
 }
}
