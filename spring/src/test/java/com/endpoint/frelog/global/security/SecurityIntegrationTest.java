package com.endpoint.frelog.global.security;

import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.http.HttpHeaders;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;

import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@SpringBootTest
class SecurityIntegrationTest {

    @Autowired
    private WebApplicationContext context;

    @Autowired
    private UserRepository userRepository;

    @Autowired
    private com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository canvasInfoRepository;

    @Autowired
    private PasswordEncoder passwordEncoder;

    @Autowired
    private JwtTokenProvider jwtTokenProvider;

    private MockMvc mockMvc;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders
                .webAppContextSetup(context)
                .apply(springSecurity())
                .build();

        canvasInfoRepository.deleteAll();
        userRepository.deleteAll();
    }

    @Test
    @DisplayName("공개 엔드포인트(/api/auth/health)는 토큰 없이 접근 가능")
    void publicEndpoint_WithoutToken_Success() throws Exception {
        mockMvc.perform(get("/api/auth/health"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("UP"));
    }

    @Test
    @DisplayName("보호된 엔드포인트(/api/auth/me)에 토큰 없이 접근 시 401 Unauthorized")
    void protectedEndpoint_WithoutToken_Unauthorized() throws Exception {
        mockMvc.perform(get("/api/auth/me"))
                .andExpect(status().isUnauthorized())
                .andExpect(jsonPath("$.code").value("AUTH_005"));
    }

    @Test
    @DisplayName("보호된 엔드포인트(/api/auth/me)에 유효한 JWT 토큰으로 접근 시 200 OK")
    void protectedEndpoint_WithValidToken_Success() throws Exception {
        // given
        User user = new User("jwtuser@agora.com", passwordEncoder.encode("secret123"), "JWT테스터", Role.ROLE_USER);
        user.setStatus(UserStatus.ACTIVE);
        User savedUser = userRepository.save(user);

        String token = jwtTokenProvider.createToken(
                savedUser.getEmail(),
                savedUser.getUserId(),
                savedUser.getNickname(),
                savedUser.getRole().name()
        );

        // when & then
        mockMvc.perform(get("/api/auth/me")
                        .header(HttpHeaders.AUTHORIZATION, "Bearer " + token))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.email").value("jwtuser@agora.com"))
                .andExpect(jsonPath("$.nickname").value("JWT테스터"));
    }

    @Test
    @DisplayName("보호된 엔드포인트에 잘못된 JWT 토큰으로 접근 시 401 Unauthorized")
    void protectedEndpoint_WithInvalidToken_Unauthorized() throws Exception {
        mockMvc.perform(get("/api/auth/me")
                        .header(HttpHeaders.AUTHORIZATION, "Bearer invalid-token-string"))
                .andExpect(status().isUnauthorized())
                .andExpect(jsonPath("$.code").value("AUTH_005"));
    }
}
