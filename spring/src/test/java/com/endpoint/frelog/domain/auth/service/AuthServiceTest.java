package com.endpoint.frelog.domain.auth.service;

import com.endpoint.frelog.domain.auth.dto.LoginRequest;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.dto.SignupRequest;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.security.crypto.password.PasswordEncoder;

import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.verify;

@ExtendWith(MockitoExtension.class)
class AuthServiceTest {

    @Mock
    private UserRepository userRepository;

    @Mock
    private PasswordEncoder passwordEncoder;

    @Mock
    private JwtTokenProvider jwtTokenProvider;

    @InjectMocks
    private AuthService authService;

    private User activeUser;

    @BeforeEach
    void setUp() {
        activeUser = new User("user@agora.com", "encodedPassword123", "아고라유저", Role.ROLE_USER);
        activeUser.setUserId(1L);
        activeUser.setStatus(UserStatus.ACTIVE);
    }

    @Test
    @DisplayName("정상 로그인 성공 시 토큰과 회원 정보 반환")
    void login_Success() {
        // given
        LoginRequest request = new LoginRequest("user@agora.com", "password123");
        given(userRepository.findByEmail(request.email())).willReturn(Optional.of(activeUser));
        given(passwordEncoder.matches("password123", "encodedPassword123")).willReturn(true);
        given(jwtTokenProvider.createToken(eq(activeUser.getEmail()), eq(1L), eq("아고라유저"), eq("ROLE_USER")))
                .willReturn("mock-jwt-token");

        // when
        LoginResponse response = authService.login(request);

        // then
        assertThat(response).isNotNull();
        assertThat(response.tokenType()).isEqualTo("Bearer");
        assertThat(response.accessToken()).isEqualTo("mock-jwt-token");
        assertThat(response.user().email()).isEqualTo("user@agora.com");
        assertThat(response.user().nickname()).isEqualTo("아고라유저");
        verify(userRepository).save(activeUser);
    }

    @Test
    @DisplayName("비밀번호 불일치 시 예외 발생")
    void login_WrongPassword_ThrowsException() {
        // given
        LoginRequest request = new LoginRequest("user@agora.com", "wrongPassword");
        given(userRepository.findByEmail(request.email())).willReturn(Optional.of(activeUser));
        given(passwordEncoder.matches("wrongPassword", "encodedPassword123")).willReturn(false);

        // when & then
        assertThatThrownBy(() -> authService.login(request))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.INVALID_CREDENTIALS);
    }

    @Test
    @DisplayName("존재하지 않는 이메일로 로그인 시 예외 발생")
    void login_UserNotFound_ThrowsException() {
        // given
        LoginRequest request = new LoginRequest("notfound@agora.com", "password123");
        given(userRepository.findByEmail(request.email())).willReturn(Optional.empty());

        // when & then
        assertThatThrownBy(() -> authService.login(request))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.INVALID_CREDENTIALS);
    }

    @Test
    @DisplayName("정지(SUSPENDED)된 계정으로 로그인 시 예외 발생")
    void login_SuspendedUser_ThrowsException() {
        // given
        activeUser.setStatus(UserStatus.SUSPENDED);
        LoginRequest request = new LoginRequest("user@agora.com", "password123");
        given(userRepository.findByEmail(request.email())).willReturn(Optional.of(activeUser));

        // when & then
        assertThatThrownBy(() -> authService.login(request))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.USER_SUSPENDED);
    }

    @Test
    @DisplayName("정상 회원가입 성공")
    void signup_Success() {
        // given
        SignupRequest request = new SignupRequest("new@agora.com", "secret123", "신규유저");
        given(userRepository.existsByEmail(request.email())).willReturn(false);
        given(passwordEncoder.encode(request.password())).willReturn("hashedSecret123");

        User savedUser = new User(request.email(), "hashedSecret123", request.nickname(), Role.ROLE_USER);
        savedUser.setUserId(2L);
        given(userRepository.save(any(User.class))).willReturn(savedUser);

        // when
        UserResponse response = authService.signup(request);

        // then
        assertThat(response).isNotNull();
        assertThat(response.email()).isEqualTo("new@agora.com");
        assertThat(response.nickname()).isEqualTo("신규유저");
    }

    @Test
    @DisplayName("이미 존재하는 이메일로 회원가입 시 예외 발생")
    void signup_DuplicateEmail_ThrowsException() {
        // given
        SignupRequest request = new SignupRequest("user@agora.com", "secret123", "중복유저");
        given(userRepository.existsByEmail(request.email())).willReturn(true);

        // when & then
        assertThatThrownBy(() -> authService.signup(request))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.EMAIL_ALREADY_EXISTS);
    }
}
