package com.endpoint.frelog.domain.auth.service;

import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.*;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.security.oauth2.core.oidc.user.OidcUser;
import java.util.Optional;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class GoogleLoginTest {
    @Mock com.endpoint.frelog.domain.user.service.UserProfileImageService profileImages;
    @Mock com.endpoint.frelog.domain.user.service.UserErasureService erasure;
    @Mock UserRepository users;
    @Mock UserSessionRepository sessions;
    @Mock PasswordEncoder passwords;
    @Mock JwtTokenProvider tokens;
    @Mock private com.endpoint.frelog.domain.auth.mfa.MfaService mfa;
    @Mock private com.endpoint.frelog.domain.auth.email.EmailVerificationService emailVerification;
    @InjectMocks AuthService auth;

    OidcUser principal() {
        OidcUser p = mock(OidcUser.class);
        when(p.getSubject()).thenReturn("google-subject");
        when(p.getEmail()).thenReturn("person@example.com");
        when(p.getEmailVerified()).thenReturn(true);
        return p;
    }
    @Test void rejectsUnverifiedEmailWithoutWriting() {
        OidcUser p = mock(OidcUser.class); when(p.getEmailVerified()).thenReturn(false);
        assertThatThrownBy(() -> auth.loginGoogle(p, new MockHttpServletRequest())).isInstanceOf(CustomException.class);
        verifyNoInteractions(users, sessions, tokens);
    }
    @Test void neverLinksAnExistingEmailAutomatically() {
        OidcUser p = principal();
        when(users.findByOauthProviderAndOauthId("google", "google-subject")).thenReturn(Optional.empty());
        when(users.existsByEmail("person@example.com")).thenReturn(true);
        assertThatThrownBy(() -> auth.loginGoogle(p, new MockHttpServletRequest())).isInstanceOf(CustomException.class);
        verify(users, never()).save(any()); verifyNoInteractions(sessions, tokens);
    }
    @Test void firstLoginCreatesOrdinaryPasswordlessUserAndSession() {
        OidcUser p = principal(); when(p.getFullName()).thenReturn("Person");
        when(users.findByOauthProviderAndOauthId("google", "google-subject")).thenReturn(Optional.empty());
        when(users.findMaxTagNumberByNicknameForUpdate("Person")).thenReturn(2);
        when(users.save(any())).thenAnswer(i -> { User u = i.getArgument(0);u.setUserId(7L);u.setOauthProvider("google");u.setOauthId("google-subject");return u; });
        when(sessions.findByIdWithPessimisticLock(7L)).thenReturn(Optional.empty());
        when(tokens.createToken(any(), any(), any(), any(), any())).thenReturn("frelog-jwt");
        var result = auth.loginGoogle(p, new MockHttpServletRequest());
        var captor = ArgumentCaptor.forClass(User.class);verify(users).save(captor.capture());
        User u = captor.getValue();assertThat(u.getRole()).isEqualTo(Role.ROLE_USER);
        assertThat(u.getPasswordHash()).isNull();assertThat(u.getOauthProvider()).isEqualTo("google");
        assertThat(u.getOauthId()).isEqualTo("google-subject");assertThat(u.getTagNumber()).isEqualTo(3);
        assertThat(result.accessToken()).isEqualTo("frelog-jwt");verify(sessions).save(any());verifyNoInteractions(passwords);
    }
    @Test void recognizesSubjectEvenWhenGoogleEmailChanges() {
        OidcUser p = principal();User u = new User("previous@example.com", null, "Person", 1, Role.ROLE_USER);u.setUserId(7L);u.setOauthProvider("google");u.setOauthId("google-subject");
        when(users.findByOauthProviderAndOauthId("google", "google-subject")).thenReturn(Optional.of(u));
        when(sessions.findByIdWithPessimisticLock(7L)).thenReturn(Optional.empty());
        auth.loginGoogle(p, new MockHttpServletRequest());
        verify(users, never()).save(any());verify(users, never()).existsByEmail(any());
        verify(tokens).createToken(eq("previous@example.com"),eq(1),eq("Person"),eq("ROLE_USER"),any());
    }
    @Test void suspendedAccountsCannotLogin() {
        OidcUser p = principal();User u = new User("person@example.com", null, "Person", 1, Role.ROLE_USER);
        when(users.findByOauthProviderAndOauthId("google", "google-subject")).thenReturn(Optional.of(u));
        u.setOauthProvider("google");u.setOauthId("google-subject");
        for (UserStatus status : new UserStatus[]{UserStatus.SUSPENDED}) {
            u.setStatus(status);assertThatThrownBy(() -> auth.loginGoogle(p, new MockHttpServletRequest())).isInstanceOf(CustomException.class);
        }
        verifyNoInteractions(sessions, tokens);
    }
    @Test void withdrawnGoogleIdentityCreatesANewUserWithoutRestoringOldPrivilegesOrMfa() {
        OidcUser p = principal(); when(p.getFullName()).thenReturn("Person");
        User old = new User("person@example.com", null, "deleted user-old", 1, Role.ROLE_ADMIN);
        old.setUserId(7L); old.setStatus(UserStatus.WITHDRAWN);
        old.setOauthProvider("google"); old.setOauthId("google-subject");
        old.setEmailVerifiedAt(java.time.LocalDateTime.now());
        when(users.findByOauthProviderAndOauthId("google", "google-subject")).thenReturn(Optional.of(old));
        doAnswer(i->{((User)i.getArgument(0)).erasePersonalData();return null;}).when(erasure).erase(old);
        when(users.save(any())).thenAnswer(i -> { User u = i.getArgument(0); u.setUserId(8L); return u; });
        when(sessions.findByIdWithPessimisticLock(8L)).thenReturn(Optional.empty());
        when(tokens.createToken(any(), any(), any(), any(), any())).thenReturn("new-user-jwt");
        var result = auth.loginGoogle(p, new MockHttpServletRequest());
        var order = inOrder(users); order.verify(users).findByOauthProviderAndOauthId("google", "google-subject");
        order.verify(users).saveAndFlush(old); order.verify(users).existsByEmail("person@example.com");
        var saved = ArgumentCaptor.forClass(User.class); verify(users).save(saved.capture());
        assertThat(saved.getValue().getUserId()).isEqualTo(8L);
        assertThat(saved.getValue().getRole()).isEqualTo(Role.ROLE_USER);
        assertThat(saved.getValue().getStatus()).isEqualTo(UserStatus.ACTIVE);
        assertThat(saved.getValue().getOauthId()).isEqualTo("google-subject");
        assertThat(old.getStatus()).isEqualTo(UserStatus.WITHDRAWN);
        assertThat(old.getOauthProvider()).isNull(); assertThat(old.getOauthId()).isNull();
        assertThat(old.getEmailVerifiedAt()).isNull();
        verify(mfa, never()).enabled(7L); verify(sessions, never()).findByIdWithPessimisticLock(7L);
        assertThat(result.accessToken()).isEqualTo("new-user-jwt");
    }

    @Test void deletingGoogleAccountReleasesUniqueIdentityWithoutDeletingHistoricalUser() {
        User old = new User("person@example.com", null, "Person", 1, Role.ROLE_USER);
        old.setUserId(7L); old.setOauthProvider("google"); old.setOauthId("google-subject");
        when(users.findByNicknameAndTagNumber("Person", 1)).thenReturn(Optional.of(old));
        when(sessions.findByIdWithPessimisticLock(7L)).thenReturn(Optional.empty());
        doAnswer(i->{((User)i.getArgument(0)).erasePersonalData();return null;}).when(erasure).erase(old);
        auth.deleteUser("Person", 1, new com.endpoint.frelog.global.security.CustomUserDetails(old));
        assertThat(old.getStatus()).isEqualTo(UserStatus.WITHDRAWN);
        assertThat(old.getEmail()).isNull();
        assertThat(old.getOauthId()).isNull(); assertThat(old.getOauthProvider()).isNull();
        assertThat(old.getUserId()).isEqualTo(7L);
        verify(users).save(old); verify(users, never()).delete(any());
    }

    @Test void googleLoginRequiresMfaBeforeIssuingAnyAccessToken() {
        OidcUser p=principal();User u=new User("person@example.com",null,"Person",1,Role.ROLE_USER);u.setUserId(7L);u.setOauthProvider("google");u.setOauthId("google-subject");
        when(users.findByOauthProviderAndOauthId("google","google-subject")).thenReturn(Optional.of(u));
        when(mfa.enabled(7L)).thenReturn(true);var request=new MockHttpServletRequest();
        when(mfa.challenge(u,request)).thenReturn(com.endpoint.frelog.domain.auth.dto.LoginResponse.challenge("opaque"));
        var result=auth.loginGoogle(p,request);assertThat(result.mfaRequired()).isTrue();assertThat(result.accessToken()).isNull();
        verifyNoInteractions(tokens,sessions);
    }
}
