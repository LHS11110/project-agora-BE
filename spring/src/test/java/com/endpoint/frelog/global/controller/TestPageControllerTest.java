package com.endpoint.frelog.global.controller;

import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import java.time.LocalDateTime;
import java.util.Optional;

import static org.mockito.BDDMockito.given;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class TestPageControllerTest {

    private MockMvc mockMvc;

    @Mock
    private ServerInfoRepository serverInfoRepository;

    @InjectMocks
    private TestPageController controller;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(controller).build();
    }

    @Test
    void rejectsUnregisteredProxyTarget() throws Exception {
        given(serverInfoRepository.findByServerIpAndServerPort("169.254.169.254", "80"))
                .willReturn(Optional.empty());

        mockMvc.perform(get("/api/test/cpp-active-canvases")
                        .param("host", "169.254.169.254")
                        .param("port", "80"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.error").exists());
    }

    @Test
    void rejectsStaleRegisteredProxyTarget() throws Exception {
        ServerInfo stale = new ServerInfo("127.0.0.1", "8000", "8002");
        stale.setLastHeartbeatAt(LocalDateTime.now().minusMinutes(1));
        given(serverInfoRepository.findByServerIpAndServerPort("127.0.0.1", "8000"))
                .willReturn(Optional.of(stale));

        mockMvc.perform(get("/api/test/cpp-canvas-count")
                        .param("host", "127.0.0.1")
                        .param("port", "8000"))
                .andExpect(status().isBadRequest());
    }
}
