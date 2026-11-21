package com.endpoint.frelog.domain.auth.oauth;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.auth.mfa.MfaService;
import com.endpoint.frelog.domain.auth.email.EmailVerificationService;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.junit.jupiter.api.extension.ExtendWith;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.mockito.*;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.security.crypto.password.PasswordEncoder;
import java.util.*;
import static org.mockito.Mockito.*;
import static org.assertj.core.api.Assertions.*;
@ExtendWith(MockitoExtension.class)
class SocialLoginTest {
 @Mock org.springframework.context.ApplicationEventPublisher events;
 @Mock UserRepository users;@Mock UserSessionRepository sessions;@Mock PasswordEncoder passwords;@Mock JwtTokenProvider tokens;@Mock com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions;@Mock MfaService mfa;@Mock EmailVerificationService emailProof;@InjectMocks AuthService auth;
 SocialIdentity identity(String provider){return new SocialIdentity(provider,"subject","person@example.invalid","Person",true);}
 User linked(String provider){var u=new User("registered@example.invalid",null,"Person",1,Role.ROLE_USER);u.setUserId(1L);u.setOauthProvider(provider);u.setOauthId("subject");return u;}
 @ParameterizedTest @ValueSource(strings={"naver","kakao"}) void firstSignupNeedsEmailProofIfProviderDidNotVerifyEmail(String provider){var i=identity(provider);assertThatThrownBy(()->auth.loginSocial(new SocialIdentity(provider,i.subject(),i.email(),i.name(),false),new MockHttpServletRequest())).isInstanceOf(OAuthEmailRequired.class);verify(users,never()).save(any());verifyNoInteractions(tokens,sessions);}
 @ParameterizedTest @ValueSource(strings={"naver","kakao"}) void existingSubjectLogsInEvenIfEmailIsNoLongerReturned(String provider){var u=linked(provider);when(users.findByOauthProviderAndOauthId(provider,"subject")).thenReturn(Optional.of(u));when(sessions.findByIdWithPessimisticLock(1L)).thenReturn(Optional.empty());auth.loginSocial(new SocialIdentity(provider,"subject",null,null,false),new MockHttpServletRequest());verify(authSessions).issue(eq(u),any(),isNull());verify(users,never()).existsByEmail(any());}
 @ParameterizedTest @ValueSource(strings={"naver","kakao"}) void mfaStillRequired(String provider){var u=linked(provider);when(users.findByOauthProviderAndOauthId(provider,"subject")).thenReturn(Optional.of(u));when(mfa.enabled(1L)).thenReturn(true);var request=new MockHttpServletRequest();when(mfa.challenge(u,request)).thenReturn(LoginResponse.challenge("challenge"));assertThat(auth.loginSocial(identity(provider),request).mfaRequired()).isTrue();verifyNoInteractions(tokens,sessions);}
 @ParameterizedTest @ValueSource(strings={"naver","kakao"}) void cannotAutoLinkMatchingEmail(String provider){when(users.existsByEmail("person@example.invalid")).thenReturn(true);assertThatThrownBy(()->auth.loginSocial(identity(provider),new MockHttpServletRequest())).isInstanceOf(com.endpoint.frelog.global.exception.CustomException.class);verify(users,never()).save(any());verifyNoInteractions(tokens,sessions);}
 @ParameterizedTest @ValueSource(strings={"naver","kakao"}) void suspendedAccountsStayBlocked(String provider){var u=linked(provider);u.setStatus(UserStatus.SUSPENDED);when(users.findByOauthProviderAndOauthId(provider,"subject")).thenReturn(Optional.of(u));assertThatThrownBy(()->auth.loginSocial(identity(provider),new MockHttpServletRequest())).isInstanceOf(com.endpoint.frelog.global.exception.CustomException.class);verifyNoInteractions(tokens,sessions);}
 @ParameterizedTest @ValueSource(strings={"naver","kakao"}) void caseInsensitiveDatabaseMatchCannotAuthenticateDifferentSubject(String provider){var u=linked(provider);u.setOauthId("SUBJECT");when(users.findByOauthProviderAndOauthId(provider,"subject")).thenReturn(Optional.of(u));assertThatThrownBy(()->auth.loginSocial(identity(provider),new MockHttpServletRequest())).isInstanceOf(com.endpoint.frelog.global.exception.CustomException.class);verifyNoInteractions(tokens,sessions);}
 @ParameterizedTest @ValueSource(strings={"naver","kakao"}) void unverifiedEmailCollisionIsNoticeBeforeEmailChallenge(String provider){var target=new User("person@example.invalid","unchanged","Existing",1,Role.ROLE_USER);when(users.findByEmail(target.getEmail())).thenReturn(Optional.of(target));assertThatThrownBy(()->auth.loginSocial(new SocialIdentity(provider,"new-subject",target.getEmail(),"New",false),new MockHttpServletRequest())).isInstanceOf(com.endpoint.frelog.global.exception.CustomException.class).hasFieldOrPropertyWithValue("errorCode",com.endpoint.frelog.global.exception.ErrorCode.EMAIL_ALREADY_EXISTS);verifyNoInteractions(emailProof,tokens,sessions);verify(users,never()).save(any());}

 @org.junit.jupiter.api.Test void newKakaoAccountUsesNicknameAndImportsPhotoOnlyOnce(){var identity=new SocialIdentity("kakao","new-id","new@example.invalid","카카오 별명",true,"https://k.kakaocdn.net/photo");when(users.save(any())).thenAnswer(a->{User u=a.getArgument(0);u.setUserId(7L);return u;});when(sessions.findByIdWithPessimisticLock(7L)).thenReturn(Optional.empty());auth.loginSocial(identity,new MockHttpServletRequest());verify(users).findMaxTagNumberByNicknameForUpdate("카카오 별명");verify(events).publishEvent(new SocialProfileImageService.ImportImage(7L,identity.profileImageUrl()));}
 @org.junit.jupiter.api.Test void existingKakaoAccountKeepsItsOwnNicknameAndPhoto(){var u=linked("kakao");when(users.findByOauthProviderAndOauthId("kakao","subject")).thenReturn(Optional.of(u));when(sessions.findByIdWithPessimisticLock(1L)).thenReturn(Optional.empty());auth.loginSocial(new SocialIdentity("kakao","subject",null,"Changed at Kakao",false,"https://k.kakaocdn.net/changed"),new MockHttpServletRequest());assertThat(u.getNickname()).isEqualTo("Person");verifyNoInteractions(events);verify(users,never()).save(any());}

}
