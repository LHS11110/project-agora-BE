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
    @Mock UserRepository users;
    @Mock UserSessionRepository sessions;
    @Mock PasswordEncoder passwords;
    @Mock JwtTokenProvider tokens;
    @InjectMocks AuthService auth;

    OidcUser principal() {
        OidcUser p = mock(OidcUser.class);
        when(p.getSubject()).thenReturn("google-subject");
        when(p.getEmail()).thenReturn("person@example.com");
        when(p.getEmailVerified()).thenReturn(true);
        return p;
    }
    @Test void rejectsUnverifiedEmailWithoutWriting() {
        OidcUser p = principal(); when(p.getEmailVerified()).thenReturn(false);
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
        when(users.save(any())).thenAnswer(i -> { User u = i.getArgument(0);u.setUserId(7L);return u; });
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
        OidcUser p = principal();User u = new User("previous@example.com", null, "Person", 1, Role.ROLE_USER);u.setUserId(7L);
        when(users.findByOauthProviderAndOauthId("google", "google-subject")).thenReturn(Optional.of(u));
        when(sessions.findByIdWithPessimisticLock(7L)).thenReturn(Optional.empty());
        auth.loginGoogle(p, new MockHttpServletRequest());
        verify(users, never()).save(any());verify(users, never()).existsByEmail(any());
        verify(tokens).createToken(eq("previous@example.com"),eq(1),eq("Person"),eq("ROLE_USER"),any());
    }
    @Test void suspendedAndWithdrawnAccountsCannotLogin() {
        OidcUser p = principal();User u = new User("person@example.com", null, "Person", 1, Role.ROLE_USER);
        when(users.findByOauthProviderAndOauthId("google", "google-subject")).thenReturn(Optional.of(u));
        for (UserStatus status : new UserStatus[]{UserStatus.SUSPENDED, UserStatus.WITHDRAWN}) {
            u.setStatus(status);assertThatThrownBy(() -> auth.loginGoogle(p, new MockHttpServletRequest())).isInstanceOf(CustomException.class);
        }
        verifyNoInteractions(sessions, tokens);
    }
}
