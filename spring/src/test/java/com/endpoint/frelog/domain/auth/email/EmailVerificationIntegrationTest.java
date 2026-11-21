package com.endpoint.frelog.domain.auth.email;

import com.endpoint.frelog.domain.auth.dto.*;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.mail.MailSendException;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;
import java.time.*;
import java.util.*;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@SpringBootTest(properties="app.email.verification-secret=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
class EmailVerificationIntegrationTest {
 @MockitoBean CppServerClient cpp;@MockitoBean CanvasRedisDocumentReader redis;@MockitoBean VerificationMailer mail;
 @TestBean(methodName="database") DataSource dataSource;
 static DataSource database(){var d=new HikariDataSource();d.setJdbcUrl("jdbc:h2:mem:email_verify;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;INIT=CREATE SCHEMA IF NOT EXISTS dbo\\;SET SCHEMA dbo");d.setUsername("sa");return d;}
 @TestBean(methodName="clock") Clock mfaClock;
 static Clock clock(){return new MutableClock();}
 static class MutableClock extends Clock {Instant value=Instant.now();public ZoneId getZone(){return ZoneOffset.UTC;}public Clock withZone(ZoneId zone){return this;}public Instant instant(){return value;}void advance(long seconds){value=value.plusSeconds(seconds);}}
 @Autowired EmailVerificationService service;@Autowired EmailProofRepository proofs;@Autowired EmailQuotaRepository quotas;
 @Autowired UserRepository users;@Autowired UserSessionRepository sessions;@Autowired AuthService auth;@Autowired WebApplicationContext context;
 List<String> codes;MockMvc mvc;final String email="owner@example.invalid";
 @BeforeEach void setup(){proofs.deleteAll();quotas.deleteAll();sessions.deleteAll();users.deleteAll();((MutableClock)mfaClock).value=Instant.now();codes=new ArrayList<>();when(mail.available()).thenReturn(true);doAnswer(i->{codes.add(i.getArgument(1));return null;}).when(mail).send(anyString(),anyString());mvc=MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();}
 void advance(long seconds){((MutableClock)mfaClock).advance(seconds);}
 SignupRequest signup(String email,String grant){return new SignupRequest(email,"Test-password12!","Owner",grant);}
 @Test void noSignupEndpointOrServiceCanCreateAccountWithoutProof() throws Exception {
  String body="{\"email\":\"owner@example.invalid\",\"password\":\"Test-password12!\",\"nickname\":\"Owner\"}";
  for(String path:new String[]{"/api/auth/signup","/api/users"})mvc.perform(post(path).contentType("application/json").content(body)).andExpect(status().isBadRequest());
  assertThatThrownBy(()->auth.signup(signup(email,null))).isInstanceOf(EmailVerificationRejected.class);assertThat(users.count()).isZero();
 }
 @Test void proofIsBoundToRecipientSingleUseAndCreatesVerifiedAccountOnlyAfterCode(){
  var requested=service.request("OWNER@example.invalid","127.0.0.1");assertThat(users.count()).isZero();assertThat(codes).hasSize(1);
  var stored=proofs.findById(email).orElseThrow();assertThat(stored.codeHash).hasSize(64);assertThat(stored.codeHash).isNotEqualTo(codes.getFirst());assertThat(stored.challengeHash).isNotEqualTo(requested.challengeId());
  var verified=service.verify(email,requested.challengeId(),codes.getFirst());assertThat(users.count()).isZero();
  assertThatThrownBy(()->service.verify(email,requested.challengeId(),codes.getFirst())).isInstanceOf(EmailVerificationRejected.class);
  assertThatThrownBy(()->auth.signup(signup("other@example.invalid",verified.emailVerificationToken()))).isInstanceOf(EmailVerificationRejected.class);
  var user=auth.signup(signup("OWNER@example.invalid",verified.emailVerificationToken()));assertThat(user.email()).isEqualTo(email);assertThat(users.findByEmail(email).orElseThrow().getEmailVerifiedAt()).isNotNull();
  assertThatThrownBy(()->auth.signup(signup(email,verified.emailVerificationToken()))).isInstanceOf(EmailVerificationRejected.class);assertThat(users.count()).isEqualTo(1);
  var request=new MockHttpServletRequest();request.setRemoteAddr("127.0.0.1");assertThat(auth.login(new LoginRequest(email,"Test-password12!"),request).accessToken()).isNotBlank();
 }
 @Test void failedSignupRollsBackProofConsumption(){
  var r=service.request(email,"127.0.0.1");String grant=service.verify(email,r.challengeId(),codes.getFirst()).emailVerificationToken();
  assertThatThrownBy(()->auth.signup(new SignupRequest(email,"한".repeat(30),"Owner",grant))).isInstanceOf(RuntimeException.class);
  assertThat(proofs.findById(email).orElseThrow().consumed).isFalse();assertThat(users.count()).isZero();auth.signup(signup(email,grant));assertThat(users.count()).isEqualTo(1);
 }
 @Test void expiryAndResendInvalidateOldProofAndChallenge(){
  var first=service.request(email,"127.0.0.1");String firstCode=codes.getFirst();assertThatThrownBy(()->service.request(email,"127.0.0.1")).isInstanceOf(EmailVerificationRejected.class);
  advance(61);var second=service.request(email,"127.0.0.1");assertThat(first.challengeId()).isNotEqualTo(second.challengeId());assertThatThrownBy(()->service.verify(email,first.challengeId(),firstCode)).isInstanceOf(EmailVerificationRejected.class);
  String grant=service.verify(email,second.challengeId(),codes.getLast()).emailVerificationToken();advance(601);assertThatThrownBy(()->auth.signup(signup(email,grant))).isInstanceOf(EmailVerificationRejected.class);assertThat(users.count()).isZero();
  var third=service.request(email,"127.0.0.1");advance(601);assertThatThrownBy(()->service.verify(email,third.challengeId(),codes.getLast())).isInstanceOf(EmailVerificationRejected.class);
 }
 @Test void wrongAttemptsPersistAndCorrectCodeCannotBypassLock(){
  var r=service.request(email,"127.0.0.1");String wrong=codes.getFirst().equals("000000")?"111111":"000000";
  for(int i=0;i<5;i++)assertThatThrownBy(()->service.verify(email,r.challengeId(),wrong)).isInstanceOf(EmailVerificationRejected.class);
  assertThat(proofs.findById(email).orElseThrow().attempts).isEqualTo(5);assertThatThrownBy(()->service.verify(email,r.challengeId(),codes.getFirst())).isInstanceOf(EmailVerificationRejected.class);assertThat(users.count()).isZero();
 }
 @Test void mailboxAndIpQuotasLimitMailAbuse(){
  for(int i=0;i<5;i++){service.request(email,"127.0.0.1");advance(61);}assertThatThrownBy(()->service.request(email,"127.0.0.1")).isInstanceOf(EmailVerificationRejected.class);assertThat(codes).hasSize(5);
  for(int i=0;i<20;i++)service.request("person"+i+"@example.invalid","192.0.2.1");assertThatThrownBy(()->service.request("overflow@example.invalid","192.0.2.1")).isInstanceOf(EmailVerificationRejected.class);
 }
 @Test void smtpFailuresDoNotCreateAccountOrResetSendQuota(){
  doThrow(new MailSendException("fixture offline")).when(mail).send(anyString(),anyString());assertThatThrownBy(()->service.request(email,"127.0.0.1")).isInstanceOf(EmailVerificationRejected.class);
  var p=proofs.findById(email).orElseThrow();assertThat(p.codeHash).isNull();assertThat(p.sends).isEqualTo(1);assertThatThrownBy(()->service.request(email,"127.0.0.1")).isInstanceOf(EmailVerificationRejected.class);assertThat(users.count()).isZero();
 }
 @Test void unavailableSenderFailsClosed(){when(mail.available()).thenReturn(false);assertThat(service.available()).isFalse();assertThatThrownBy(()->service.request(email,"127.0.0.1")).isInstanceOf(EmailVerificationRejected.class);assertThat(proofs.count()).isZero();verify(mail,never()).send(anyString(),anyString());}
 @Test void registeredAccountsRemainUntouchedAndDoNotReceiveSignupCodes(){
  var user=users.save(new com.endpoint.frelog.domain.user.entity.User(email,"unchanged-password-hash","Existing",1,com.endpoint.frelog.domain.user.entity.Role.ROLE_USER));
  assertThatThrownBy(()->service.request(email,"127.0.0.1")).isInstanceOf(com.endpoint.frelog.global.exception.CustomException.class).hasFieldOrPropertyWithValue("errorCode",com.endpoint.frelog.global.exception.ErrorCode.EMAIL_ALREADY_EXISTS);
  verify(mail,never()).send(anyString(),anyString());
  assertThat(users.findById(user.getUserId()).orElseThrow().getPasswordHash()).isEqualTo("unchanged-password-hash");assertThat(users.findById(user.getUserId()).orElseThrow().getEmailVerifiedAt()).isNull();
 }
}
