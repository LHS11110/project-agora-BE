package com.endpoint.frelog.global.controller;

import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.domain.user.entity.UserSession;
import com.endpoint.frelog.domain.user.repository.UserSessionRepository;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import java.util.List;

import static org.mockito.BDDMockito.given;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class TestPageControllerTest {

    private MockMvc mockMvc;

    @Mock
    private UserSessionRepository userSessionRepository;

    @InjectMocks
    private TestPageController controller;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(controller).build();
    }

    @Test
    void returnsDatabaseBackedActiveCanvasList() throws Exception {
        UserSession first = new UserSession();
        first.setIsAccessed(true);
        first.setCanvas(new CanvasInfo(42));
        UserSession second = new UserSession();
        second.setIsAccessed(true);
        second.setCanvas(new CanvasInfo(42));
        given(userSessionRepository.findByIsAccessedTrue()).willReturn(List.of(first, second));

        mockMvc.perform(get("/api/test/cpp-active-canvases"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("success"))
                .andExpect(jsonPath("$.count").value(1))
                .andExpect(jsonPath("$.canvases[0].canvas_id").value(42))
                .andExpect(jsonPath("$.canvases[0].active_user_count").value(2));
    }

    @Test
    void returnsDatabaseBackedActiveCanvasCount() throws Exception {
        UserSession first = new UserSession();
        first.setIsAccessed(true);
        first.setCanvas(new CanvasInfo(42));
        UserSession second = new UserSession();
        second.setIsAccessed(true);
        second.setCanvas(new CanvasInfo(43));
        given(userSessionRepository.findByIsAccessedTrue()).willReturn(List.of(first, second));

        mockMvc.perform(get("/api/test/cpp-canvas-count"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("success"))
                .andExpect(jsonPath("$.count").value(2));
    }
}
