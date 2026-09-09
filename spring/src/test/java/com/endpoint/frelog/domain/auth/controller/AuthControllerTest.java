package com.endpoint.frelog.domain.auth.controller;

import com.endpoint.frelog.domain.auth.dto.LoginRequest;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.dto.SignupRequest;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.exception.GlobalExceptionHandler;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import java.time.LocalDateTime;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.BDDMockito.given;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class AuthControllerTest {

    private MockMvc mockMvc;

    private final ObjectMapper objectMapper = new ObjectMapper();

    @Mock
    private AuthService authService;

    @InjectMocks
    private AuthController authController;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(authController)
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    @DisplayName("로그인 API 성공 시 200 OK 및 토큰 반환")
    void loginApi_Success() throws Exception {
        // given
        LoginRequest request = new LoginRequest("user@agora.com", "password123");
        UserResponse userResponse = new UserResponse(
                1L, "user@agora.com", "아고라유저", Role.ROLE_USER, UserStatus.ACTIVE, LocalDateTime.now(), LocalDateTime.now()
        );
        LoginResponse response = LoginResponse.of("mock-access-token", userResponse);

        given(authService.login(any(LoginRequest.class))).willReturn(response);

        // when & then
        mockMvc.perform(post("/api/auth/login")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(request)))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.tokenType").value("Bearer"))
                .andExpect(jsonPath("$.accessToken").value("mock-access-token"))
                .andExpect(jsonPath("$.user.email").value("user@agora.com"));
    }

    @Test
    @DisplayName("로그인 API 비밀번호 불일치 시 401 Unauthorized 반환")
    void loginApi_InvalidCredentials() throws Exception {
        // given
        LoginRequest request = new LoginRequest("user@agora.com", "wrongPass");
        given(authService.login(any(LoginRequest.class)))
                .willThrow(new CustomException(ErrorCode.INVALID_CREDENTIALS));

        // when & then
        mockMvc.perform(post("/api/auth/login")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(request)))
                .andExpect(status().isUnauthorized())
                .andExpect(jsonPath("$.code").value("AUTH_001"));
    }

    @Test
    @DisplayName("회원가입 API 성공 시 201 Created 반환")
    void signupApi_Success() throws Exception {
        // given
        SignupRequest request = new SignupRequest("new@agora.com", "password123", "새유저");
        UserResponse userResponse = new UserResponse(
                2L, "new@agora.com", "새유저", Role.ROLE_USER, UserStatus.ACTIVE, null, LocalDateTime.now()
        );

        given(authService.signup(any(SignupRequest.class))).willReturn(userResponse);

        // when & then
        mockMvc.perform(post("/api/auth/signup")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(request)))
                .andExpect(status().isCreated())
                .andExpect(jsonPath("$.email").value("new@agora.com"))
                .andExpect(jsonPath("$.nickname").value("새유저"));
    }

    @Test
    @DisplayName("헬스체크 API는 200 OK")
    void healthCheckApi() throws Exception {
        mockMvc.perform(get("/api/auth/health"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("UP"));
    }
}

