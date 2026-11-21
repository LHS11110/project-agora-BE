package com.endpoint.frelog.global.security;

import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.auth.email.*;
import com.endpoint.frelog.domain.auth.mfa.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.zaxxer.hikari.HikariDataSource;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import javax.sql.DataSource;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.mock.web.*;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.*;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;
import org.springframework.security.oauth2.client.registration.*;
import org.springframework.security.oauth2.core.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.CopyOnWriteArrayList;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@SpringBootTest(properties={"app.oauth.naver.enabled=true","app.security.cors.allowed-origins=https://frelog.p-e.kr","app.oauth.kakao.enabled=true","app.mfa.encryption-key=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","app.email.verification-secret=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="})
class SocialOAuthIntegrationTest {
 @MockitoBean CppServerClient cpp;@MockitoBean CanvasRedisDocumentReader redis;@MockitoBean VerificationMailer mail;
 @TestBean(methodName="database") DataSource dataSource;
 static DataSource database(){var d=new HikariDataSource();d.setJdbcUrl("jdbc:h2:mem:social_oauth;MODE=MSSQLServer;DB_CLOSE_DELAY=-1");d.setUsername("sa");return d;}
 static HttpServer vendor;
 static List<String> tokenForms=new CopyOnWriteArrayList<>();
 static String naverSubject="naver-subject",naverEmail="naver@example.invalid",kakaoEmail="kakao@example.invalid";
 static long kakaoSubject=123456789L;
 static ClientRegistration registration(String id,int port){return ClientRegistration.withRegistrationId(id).clientId(id+"-id").clientSecret(id+"-secret").clientAuthenticationMethod(ClientAuthenticationMethod.CLIENT_SECRET_POST).authorizationGrantType(AuthorizationGrantType.AUTHORIZATION_CODE).clientSettings(ClientRegistration.ClientSettings.builder().requireProofKey(!id.equals("naver")).build()).redirectUri("https://frelog.p-e.kr/api/auth/oauth2/callback/"+id).authorizationUri("https://vendor.example/"+id+"/authorize").tokenUri("http://127.0.0.1:"+port+"/"+id+"/token").userInfoUri("http://127.0.0.1:"+port+"/"+id+"/me").userNameAttributeName("id").build();}
 @TestBean(methodName="registrations") ClientRegistrationRepository googleRegistration;
 static ClientRegistrationRepository registrations() throws Exception {
  vendor=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
  vendor.createContext("/",exchange->{String path=exchange.getRequestURI().getPath();String body;
   if(path.endsWith("/token")){tokenForms.add(new String(exchange.getRequestBody().readAllBytes(),StandardCharsets.UTF_8));body="{\"access_token\":\"fixture-access-token\",\"token_type\":\"bearer\",\"expires_in\":3600}";}
   else if(path.startsWith("/naver"))body="{\"resultcode\":\"00\",\"response\":{\"id\":\""+naverSubject+"\",\"email\":\""+naverEmail+"\",\"nickname\":\"Naver fixture\"}}";
   else body="{\"id\":"+kakaoSubject+",\"kakao_account\":{\"email\":\""+kakaoEmail+"\",\"is_email_valid\":true,\"is_email_verified\":true,\"profile\":{\"nickname\":\"Kakao fixture\"}}}";
   byte[] bytes=body.getBytes(StandardCharsets.UTF_8);exchange.getResponseHeaders().set("Content-Type","application/json");exchange.sendResponseHeaders(200,bytes.length);exchange.getResponseBody().write(bytes);exchange.close();
  });vendor.start();return new InMemoryClientRegistrationRepository(registration("naver",vendor.getAddress().getPort()),registration("kakao",vendor.getAddress().getPort()));
 }
 @AfterAll static void stop(){if(vendor!=null)vendor.stop(0);}
 @org.springframework.beans.factory.annotation.Autowired com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions;
 @Autowired WebApplicationContext context;@Autowired UserRepository users;@Autowired AuthService auth;@Autowired EmailVerificationService emailProof;@Autowired MfaService mfa;@Autowired JwtTokenProvider tokens;@Autowired org.springframework.security.crypto.password.PasswordEncoder passwords;@Autowired EmailProofRepository proofs;
 MockMvc mvc;ObjectMapper json=new ObjectMapper();List<String> codes;
 @BeforeEach void setup(){mvc=MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();codes=new ArrayList<>();when(mail.available()).thenReturn(true);doAnswer(i->{codes.add(i.getArgument(1));return null;}).when(mail).send(anyString(),anyString());}
 record Flow(MockHttpSession session,String state){}
 Flow start(String provider) throws Exception {
  var result=mvc.perform(get("/api/auth/oauth2/authorize/"+provider).secure(true).header("Host","internal.example:8444")).andExpect(status().is3xxRedirection()).andReturn();
  String location=result.getResponse().getHeader("Location");assertThat(location).contains("redirect_uri=https://frelog.p-e.kr/api/auth/oauth2/callback/"+provider).doesNotContain(provider+"-secret","8444");
  if(provider.equals("naver"))assertThat(location).doesNotContain("code_challenge=");else assertThat(location).contains("code_challenge=","code_challenge_method=S256");
  String state=Arrays.stream(URI.create(location).getRawQuery().split("&")).filter(s->s.startsWith("state=")).map(s->URLDecoder.decode(s.substring(6),StandardCharsets.UTF_8)).findFirst().orElseThrow();
  return new Flow((MockHttpSession)result.getRequest().getSession(false),state);
 }
 MvcResult callback(String provider,Flow flow) throws Exception {return mvc.perform(get("/api/auth/oauth2/callback/"+provider).secure(true).session(flow.session).param("state",flow.state).param("code","fixture-code")).andExpect(redirectedUrl("https://frelog.p-e.kr/login?oauth=success")).andReturn();}
 @Test void naverFullExchangeRequiresFirstEmailProofThenKeepsMfaOnReturningLogin() throws Exception {
  var flow=start("naver");var result=callback("naver",flow);assertThat(flow.session.isInvalid()).isTrue();
  var session=(MockHttpSession)result.getRequest().getSession(false);
  var handoff=mvc.perform(get("/api/auth/oauth2/session").session(session)).andExpect(status().isOk()).andExpect(jsonPath("$.oauthEnrollmentRequired").value(true)).andExpect(header().string("Cache-Control","no-store")).andReturn();
  assertThat(users.findByEmail("naver@example.invalid")).isEmpty();
  String nonce=json.readTree(handoff.getResponse().getContentAsString()).get("enrollmentToken").asText();
  String email="confirmed-naver@example.invalid";
  var sent=emailProof.request(email,"127.0.0.1");String proof=emailProof.verify(email,sent.challengeId(),codes.getLast()).emailVerificationToken();
  String body=json.writeValueAsString(Map.of("email",email,"emailVerificationToken",proof,"enrollmentToken",nonce));
  mvc.perform(post("/api/auth/oauth2/enrollment").session(session).header("Origin","https://attacker.example").contentType("application/json").content(body)).andExpect(status().is4xxClientError());
  var enrolled=mvc.perform(post("/api/auth/oauth2/enrollment").session(session).header("Origin","https://frelog.p-e.kr").contentType("application/json").content(body)).andExpect(status().isOk()).andReturn();
  assertThat(session.isInvalid()).isTrue();var user=users.findByEmail(email).orElseThrow();assertThat(user.getOauthProvider()).isEqualTo("naver");assertThat(user.getOauthId()).isEqualTo(naverSubject);assertThat(user.getEmailVerifiedAt()).isNotNull();assertThat(user.getPasswordHash()).isNull();
  String jwt=json.readTree(enrolled.getResponse().getContentAsString()).get("accessToken").asText();var request=new MockHttpServletRequest();request.setRemoteAddr("127.0.0.1");var enrollment=mfa.begin(user.getUserId(),jwt);mfa.confirm(user.getUserId(),Totp.code(enrollment.get("secret"),System.currentTimeMillis()/30000),jwt,request);
  var returning=callback("naver",start("naver"));var mfaSession=(MockHttpSession)returning.getRequest().getSession(false);
  mvc.perform(get("/api/auth/oauth2/session").session(mfaSession)).andExpect(status().isOk()).andExpect(jsonPath("$.mfaRequired").value(true)).andExpect(jsonPath("$.accessToken").isEmpty());
  assertThat(tokenForms.stream().filter(s->s.contains("client_id=naver-id")).toList()).allSatisfy(s->assertThat(s).contains("client_secret=naver-secret","state=","code=fixture-code").doesNotContain("code_verifier="));
 }
 @Test void kakaoVerifiedEmailCreatesAccountAndDeletionAllowsFreshRejoin() throws Exception {
  var result=callback("kakao",start("kakao"));var session=(MockHttpSession)result.getRequest().getSession(false);
  mvc.perform(get("/api/auth/oauth2/session").session(session)).andExpect(status().isOk()).andExpect(jsonPath("$.accessToken").isNotEmpty());
  var user=users.findByEmail(kakaoEmail).orElseThrow();assertThat(user.getOauthProvider()).isEqualTo("kakao");assertThat(user.getOauthId()).isEqualTo("123456789");assertThat(user.getEmailVerifiedAt()).isNotNull();
  auth.deleteUser(user.getNickname(),user.getTagNumber(),new CustomUserDetails(user));assertThat(users.findById(user.getUserId()).orElseThrow().getOauthId()).isNull();
  callback("kakao",start("kakao"));assertThat(users.findByEmail(kakaoEmail).orElseThrow().getUserId()).isNotEqualTo(user.getUserId());
  assertThat(tokenForms.stream().filter(s->s.contains("client_id=kakao-id")).toList()).allSatisfy(s->assertThat(s).contains("client_secret=kakao-secret","code_verifier="));
 }
 @Test void activeEmailCollisionIsAnAccountExistsNoticeForNaverAndKakao() throws Exception {
  String oldNaver=naverSubject,oldNaverEmail=naverEmail,oldKakaoEmail=kakaoEmail;long oldKakao=kakaoSubject;
  naverSubject="duplicate-naver-subject";naverEmail="duplicate-local@naver.com";kakaoSubject=987654321L;kakaoEmail="duplicate-local@gmail.com";
  try {
   for(String provider:List.of("naver","kakao")){
    String email=provider.equals("naver")?naverEmail:kakaoEmail;
    var original=users.save(new com.endpoint.frelog.domain.user.entity.User(email,"unchanged-hash","Duplicate "+provider,1,com.endpoint.frelog.domain.user.entity.Role.ROLE_USER));
    var flow=start(provider);
    mvc.perform(get("/api/auth/oauth2/callback/"+provider).secure(true).session(flow.session).param("state",flow.state).param("code","fixture-code")).andExpect(redirectedUrl("https://frelog.p-e.kr/login?oauth=account_exists"));
    assertThat(flow.session.isInvalid()).isTrue();assertThat(codes).isEmpty();
    var unchanged=users.findById(original.getUserId()).orElseThrow();assertThat(unchanged.getOauthProvider()).isNull();assertThat(unchanged.getPasswordHash()).isEqualTo("unchanged-hash");
   }
  } finally {naverSubject=oldNaver;naverEmail=oldNaverEmail;kakaoSubject=oldKakao;kakaoEmail=oldKakaoEmail;}
 }
 @Test void selectingRegisteredGmailDuringKakaoEnrollmentDoesNotRequestMailOrLink() throws Exception {
  String oldEmail=kakaoEmail;long oldSubject=kakaoSubject;kakaoEmail="";kakaoSubject=987650001L;
  try {
   String email="existing-selected@gmail.com";
   var original=users.save(new com.endpoint.frelog.domain.user.entity.User(email,"unchanged-hash","Selected Gmail",1,com.endpoint.frelog.domain.user.entity.Role.ROLE_USER));
   var result=callback("kakao",start("kakao"));var session=(MockHttpSession)result.getRequest().getSession(false);
   var handoff=mvc.perform(get("/api/auth/oauth2/session").session(session)).andExpect(status().isOk()).andReturn();
   String nonce=json.readTree(handoff.getResponse().getContentAsString()).get("enrollmentToken").asText();
   String address=json.writeValueAsString(Map.of("email",email,"enrollmentToken",nonce));
   mvc.perform(post("/api/auth/oauth2/email/request").session(session).header("Origin","https://frelog.p-e.kr").contentType("application/json").content(address)).andExpect(status().isConflict()).andExpect(jsonPath("$.code").value("USER_001"));
   String body=json.writeValueAsString(Map.of("email",email,"emailVerificationToken","not-consumed","enrollmentToken",nonce));
   String jwt=com.endpoint.frelog.TestSessions.token(authSessions,original,"127.0.0.1");
   mvc.perform(post("/api/auth/oauth2/enrollment").session(session).header("Origin","https://frelog.p-e.kr").header("Authorization","Bearer "+jwt).contentType("application/json").content(body)).andExpect(status().isConflict()).andExpect(jsonPath("$.code").value("USER_001"));
   assertThat(codes).isEmpty();assertThat(users.findById(original.getUserId()).orElseThrow().getOauthProvider()).isNull();
  } finally {kakaoEmail=oldEmail;kakaoSubject=oldSubject;}
 }
 @Test void wrongStateCannotReachTokenEndpoint() throws Exception {var flow=start("naver");int before=tokenForms.size();mvc.perform(get("/api/auth/oauth2/callback/naver").secure(true).session(flow.session).param("state","wrong").param("code","fixture-code")).andExpect(redirectedUrl("https://frelog.p-e.kr/login?oauth=failed"));assertThat(tokenForms).hasSize(before);}
 @Test void discoveryReportsAllEnabledProviders() throws Exception {mvc.perform(get("/api/auth/providers")).andExpect(jsonPath("$.naver").value(true)).andExpect(jsonPath("$.kakao").value(true)).andExpect(jsonPath("$.google").value(false));}
}
