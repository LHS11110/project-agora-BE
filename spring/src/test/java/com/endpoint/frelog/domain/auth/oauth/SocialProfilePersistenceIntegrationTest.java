package com.endpoint.frelog.domain.auth.oauth;

import com.endpoint.frelog.domain.auth.email.*;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.service.UserProfileImageService;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.CanvasRedisDocumentReader;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.*;
import org.springframework.test.util.*;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import java.io.ByteArrayOutputStream;
import java.net.URI;
import java.nio.file.*;
import java.util.*;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;

@SpringBootTest(properties="app.email.verification-secret=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
class SocialProfilePersistenceIntegrationTest {
 @MockitoBean CppServerClient cpp;@MockitoBean CanvasRedisDocumentReader redis;@MockitoBean VerificationMailer mail;
 @MockitoSpyBean SocialProfileImageService importer;
 @TestBean(methodName="database") DataSource dataSource;
 static DataSource database(){var d=new HikariDataSource();d.setJdbcUrl("jdbc:h2:mem:social_profile;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;INIT=CREATE SCHEMA IF NOT EXISTS dbo\\;SET SCHEMA dbo");d.setUsername("sa");return d;}
 @Autowired AuthService auth;@Autowired UserRepository users;@Autowired UserProfileImageService images;@Autowired EmailVerificationService email;@Autowired PlatformTransactionManager manager;
 @TempDir Path directory;List<String> codes;
 byte[] png(int color) throws Exception {var image=new BufferedImage(2,2,BufferedImage.TYPE_INT_RGB);image.setRGB(0,0,color);var out=new ByteArrayOutputStream();ImageIO.write(image,"png",out);return out.toByteArray();}
 @BeforeEach void setup() throws Exception {
  ReflectionTestUtils.setField((Object)AopTestUtils.getUltimateTargetObject(images),"directory",directory);
  doReturn(png(0xff00)).when(importer).download(any(URI.class));
  codes=new ArrayList<>();when(mail.available()).thenReturn(true);doAnswer(a->{codes.add(a.getArgument(1));return null;}).when(mail).send(anyString(),anyString());
 }
 MockHttpServletRequest request(){var r=new MockHttpServletRequest();r.setRemoteAddr("127.0.0.1");return r;}
 @ParameterizedTest @ValueSource(strings={"google","naver","kakao"})
 void profilePersistsOnlyAfterSignupAndManualChangesRemainAuthoritative(String provider) throws Exception {
  String address=provider+"-profile@example.invalid",name=provider+" 별명",url=switch(provider){case "google"->"https://lh3.googleusercontent.com/photo";case "naver"->"https://phinf.pstatic.net/photo";default->"https://k.kakaocdn.net/photo";};
  var identity=new SocialIdentity(provider,"profile-"+provider,address,name,!provider.equals("naver"),url);
  if(provider.equals("naver")){
   assertThatThrownBy(()->auth.loginSocial(identity,request())).isInstanceOf(OAuthEmailRequired.class);verify(importer,never()).download(any());
   var start=email.request(address,"127.0.0.1");var proof=email.verify(address,start.challengeId(),codes.getLast()).emailVerificationToken();auth.completeOAuthEnrollment(identity,address,proof,request());
  }else auth.loginSocial(identity,request());
  var u=users.findByEmail(address).orElseThrow();assertThat(u.getNickname()).isEqualTo(name);assertThat(u.getProfileImageUrl()).isEqualTo(images.url(u.getUserId()));assertThat(u.getProfileImageUrl()).startsWith("/api/users/profile-images/");
  Path file=directory.resolve(u.getUserId()+".png");assertThat(file).exists();assertThat(ImageIO.read(file.toFile())).isNotNull();verify(importer,times(1)).download(any());
  images.saveBytes(u.getUserId(),png(0xff0000));byte[] custom=Files.readAllBytes(file);
  auth.loginSocial(new SocialIdentity(provider,identity.subject(),address,"Changed upstream",true,url),request());assertThat(Files.readAllBytes(file)).isEqualTo(custom);assertThat(users.findById(u.getUserId()).orElseThrow().getNickname()).isEqualTo(name);verify(importer,times(1)).download(any());
  images.remove(u.getUserId());assertThat(users.findById(u.getUserId()).orElseThrow().getProfileImageUrl()).isNull();assertThat(file).doesNotExist();
  images.saveBytes(u.getUserId(),png(1));auth.deleteUser(u.getNickname(),u.getTagNumber(),new CustomUserDetails(u));assertThat(file).doesNotExist();assertThat(users.findById(u.getUserId()).orElseThrow().getProfileImageUrl()).isNull();
 }
 @Test void rolledBackSignupDoesNotLeaveAnImage() throws Exception {
  new TransactionTemplate(manager).executeWithoutResult(status->{auth.loginSocial(new SocialIdentity("google","rollback-profile","rollback-photo@example.invalid","Rollback",true,"https://lh3.googleusercontent.com/photo"),request());status.setRollbackOnly();});
  assertThat(users.findByEmail("rollback-photo@example.invalid")).isEmpty();verify(importer,never()).download(any());try(var files=Files.list(directory)){assertThat(files.count()).isZero();}
 }
 @Test void unavailablePhotoKeepsAccountAndNickname() throws Exception {
  doThrow(new java.io.IOException("fixture")).when(importer).download(any());
  auth.loginSocial(new SocialIdentity("google","unavailable-photo","unavailable-photo@example.invalid","Available name",true,"https://lh3.googleusercontent.com/photo"),request());
  var u=users.findByEmail("unavailable-photo@example.invalid").orElseThrow();assertThat(u.getNickname()).isEqualTo("Available name");assertThat(u.getProfileImageUrl()).isNull();
 }
}
