package com.endpoint.frelog.domain.user.service;

import com.endpoint.frelog.domain.auth.dto.*;
import com.endpoint.frelog.domain.auth.email.*;
import com.endpoint.frelog.domain.auth.mfa.*;
import com.endpoint.frelog.domain.auth.oauth.*;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.endpoint.frelog.global.security.*;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.*;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.*;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;
import java.time.*;
import java.util.*;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@SpringBootTest(properties={"app.mfa.encryption-key=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","app.email.verification-secret=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="})
class UserErasureIntegrationTest {
 @MockitoBean CppServerClient cpp;@MockitoBean CanvasRedisDocumentReader redis;@MockitoBean VerificationMailer mail;@MockitoBean UserProfileImageService images;
 @TestBean(methodName="database") DataSource dataSource;
 static DataSource database(){var db=new HikariDataSource();db.setJdbcUrl("jdbc:h2:mem:user_erasure;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;INIT=CREATE SCHEMA IF NOT EXISTS dbo\\;SET SCHEMA dbo");db.setUsername("sa");return db;}
 @Autowired AuthService auth;@Autowired UserRepository users;@Autowired EmailVerificationService email;@Autowired MfaService mfa;@Autowired JwtTokenProvider tokens;@Autowired WebApplicationContext context;
 MockMvc mvc;JdbcTemplate sql;List<String> codes;
 @BeforeEach void setup(){mvc=MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();sql=new JdbcTemplate(dataSource);codes=new ArrayList<>();when(mail.available()).thenReturn(true);doAnswer(i->{codes.add(i.getArgument(1));return null;}).when(mail).send(anyString(),anyString());}
 String proof(String address){var start=email.request(address,"127.0.0.1");return email.verify(address,start.challengeId(),codes.getLast()).emailVerificationToken();}
 MockHttpServletRequest request(){var r=new MockHttpServletRequest();r.setRemoteAddr("127.0.0.1");return r;}
 void tombstone(Long id){var u=users.findById(id).orElseThrow();assertThat(u.getStatus()).isEqualTo(UserStatus.WITHDRAWN);assertThat(u.getNickname()).isEqualTo("삭제된 계정");assertThat(u.getTagNumber()).isEqualTo(id.intValue());assertThat(u.getEmail()).isNull();assertThat(u.getPasswordHash()).isNull();assertThat(u.getOauthProvider()).isNull();assertThat(u.getOauthId()).isNull();assertThat(u.getEmailVerifiedAt()).isNull();assertThat(u.getPasswordChangedAt()).isNull();assertThat(u.getRole()).isEqualTo(Role.ROLE_USER);}
 @Test void withdrawalPurgesAuthDataAndReusedEmailDoesNotAcceptOldAccountTokens() throws Exception {
  String address="erasure-local@example.invalid",password="Erasure-password12!",nickname="Erasure local";
  auth.signup(new SignupRequest(address,password,nickname,proof(address)));var old=users.findByEmail(address).orElseThrow();Long id=old.getUserId();
  String original=auth.login(new LoginRequest(address,password),request()).accessToken();assertThat(tokens.getUserIdFromToken(original)).isEqualTo(id);
  var enrollment=mfa.begin(id,original);String mfaJwt=mfa.confirm(id,Totp.code(enrollment.get("secret"),System.currentTimeMillis()/30000),original,request()).login().accessToken();auth.login(new LoginRequest(address,password),request());
  sql.update("insert into user_passkeys (id,credential_hash,credential_id,user_id,user_handle,public_key,signature_count,name,created_at) values (?,?,?,?,?,?,0,?,CURRENT_TIMESTAMP)","key-"+id,"hash-"+id,"credential-"+id,id,"handle-"+id,"public-"+id,"Personal device");
  sql.update("insert into auth_passkey_challenges(id,purpose,user_id,client_ip,request_json,created_at,expires_at,used) values (?,?,?,?,?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP,false)","challenge-"+id,"REGISTER",id,"127.0.0.1","private-profile-fixture");
  auth.deleteUser(old.getNickname(),old.getTagNumber(),new CustomUserDetails(old));tombstone(id);verify(images).remove(id);
  for(String table:List.of("user_mfa","auth_mfa_challenges","user_passkeys","auth_passkey_challenges","user_sessions"))assertThat(sql.queryForObject("select count(*) from "+table+" where user_id=?",Integer.class,id)).as(table).isZero();
  assertThat(sql.queryForObject("select count(*) from auth_email_verification where email=?",Integer.class,address)).isZero();
  auth.signup(new SignupRequest(address,password,nickname,proof(address)));var replacement=users.findByEmail(address).orElseThrow();assertThat(replacement.getUserId()).isNotEqualTo(id);
  String fresh=auth.login(new LoginRequest(address,password),request()).accessToken();assertThat(tokens.getUserIdFromToken(fresh)).isEqualTo(replacement.getUserId());
  for(String stale:List.of(original,mfaJwt)){
   mvc.perform(get("/api/auth/passkeys").header("Authorization","Bearer "+stale)).andExpect(status().isUnauthorized());
   mvc.perform(post("/api/auth/me").contentType("application/json").content("{\"token\":\""+stale+"\"}")).andExpect(status().isUnauthorized());
  }
  mvc.perform(get("/api/auth/passkeys").header("Authorization","Bearer "+fresh)).andExpect(status().isOk());
 }
 @ParameterizedTest @ValueSource(strings={"google","naver","kakao"}) void everySocialProviderCanRejoinWithoutRestoringOldPersonalData(String provider){
  String address="erasure-"+provider+"@example.invalid",subject="erased-"+provider;
  var old=new User(address,null,"Old "+provider,1,Role.ROLE_ADMIN);old.setOauthProvider(provider);old.setOauthId(subject);old=users.save(old);Long id=old.getUserId();
  auth.deleteUser(old.getNickname(),old.getTagNumber(),new CustomUserDetails(old));tombstone(id);
  var identity=new SocialIdentity(provider,subject,address,"New "+provider,!provider.equals("naver"));
  if(provider.equals("naver")){
   assertThatThrownBy(()->auth.loginSocial(identity,request())).isInstanceOf(OAuthEmailRequired.class);
   auth.completeOAuthEnrollment(identity,address,proof(address),request());
  }else auth.loginSocial(identity,request());
  var fresh=users.findByEmail(address).orElseThrow();assertThat(fresh.getUserId()).isNotEqualTo(id);assertThat(fresh.getRole()).isEqualTo(Role.ROLE_USER);assertThat(fresh.getOauthId()).isEqualTo(subject);tombstone(id);
 }
}
