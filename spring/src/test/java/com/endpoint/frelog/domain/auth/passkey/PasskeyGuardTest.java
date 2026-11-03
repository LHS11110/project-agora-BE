package com.endpoint.frelog.domain.auth.passkey;

import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.junit.jupiter.api.*;
import org.springframework.mock.web.MockHttpServletRequest;
import java.time.*;
import java.util.*;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;

class PasskeyGuardTest {
    PasskeyCredentialRepository keys=mock(PasskeyCredentialRepository.class);
    PasskeyChallengeRepository challenges=mock(PasskeyChallengeRepository.class);
    UserRepository users=mock(UserRepository.class);
    JwtTokenProvider tokens=mock(JwtTokenProvider.class);
    AuthService auth=mock(AuthService.class);
    PasskeyStore store=mock(PasskeyStore.class);
    Instant instant=Instant.parse("2026-11-01T00:00:00Z");
    PasskeyService service=new PasskeyService(keys,challenges,users,tokens,auth,Clock.fixed(instant,ZoneOffset.UTC),store,"frelog.p-e.kr","https://frelog.p-e.kr");
    MockHttpServletRequest request=new MockHttpServletRequest();
    @BeforeEach void setup(){request.setRemoteAddr("127.0.0.1");}
    PasskeyChallenge challenge(){var c=new PasskeyChallenge();c.id=PasskeyService.hash("opaque");c.clientIp="127.0.0.1";c.purpose="LOGIN";c.expiresAt=LocalDateTime.ofInstant(instant.plusSeconds(300),ZoneOffset.UTC);when(challenges.lock(c.id)).thenReturn(Optional.of(c));return c;}
    void rejected(){assertThatThrownBy(()->service.login("opaque","opaque","{}",request)).isInstanceOf(PasskeyRejected.class);verifyNoInteractions(auth,keys);}
    @Test void expiredChallengeCannotReachSignatureOrLogin(){challenge().expiresAt=LocalDateTime.ofInstant(instant.minusSeconds(1),ZoneOffset.UTC);rejected();}
    @Test void usedChallengeCannotReachSignatureOrLogin(){challenge().used=true;rejected();}
    @Test void ipChangeCannotReachSignatureOrLogin(){challenge().clientIp="192.0.2.10";rejected();}
    @Test void registrationChallengeCannotBecomeLogin(){challenge().purpose="REGISTER";rejected();}
    @Test void missingBrowserCookieCannotReachDatabase(){assertThatThrownBy(()->service.login("opaque",null,"{}",request)).isInstanceOf(PasskeyRejected.class);verifyNoInteractions(challenges,keys,auth);}
    @Test void requestBelongsToRegistrationAccount(){var c=challenge();c.userId=2L;c.purpose="REGISTER";when(tokens.getIssuedAt("jwt")).thenReturn(Date.from(instant));assertThatThrownBy(()->service.register(1L,"jwt","opaque","opaque","{}","Key",request)).isInstanceOf(PasskeyRejected.class);verifyNoInteractions(users,keys,auth);}
    @Test void staleSessionCannotRegisterOrDelete(){when(tokens.getIssuedAt("old")).thenReturn(Date.from(instant.minusSeconds(601)));assertThatThrownBy(()->service.registerOptions(1L,"old",request)).isInstanceOf(PasskeyRejected.class);assertThatThrownBy(()->service.delete(1L,"key","old")).isInstanceOf(PasskeyRejected.class);verifyNoInteractions(users,challenges,keys);}
    @Test void forwardedHostDoesNotAuthorizeOrigin(){request.addHeader("Origin","https://attacker.example");request.addHeader("X-Forwarded-Host","frelog.p-e.kr");assertThatThrownBy(()->service.checkOrigin(request)).isInstanceOf(PasskeyRejected.class);}
    @Test void optionsAreThrottledBeforeCreatingPersistentChallenge(){when(challenges.countByClientIpAndCreatedAtAfter(anyString(),any())).thenReturn(20L);assertThatThrownBy(()->service.loginOptions(request)).isInstanceOf(PasskeyRejected.class);verify(challenges,never()).save(any());}
}
