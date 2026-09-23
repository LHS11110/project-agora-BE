package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasSummaryResponse;
import com.endpoint.frelog.domain.canvas.service.CanvasResourceService;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
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

import java.util.List;
import java.util.Map;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.BDDMockito.given;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.delete;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.patch;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;
import static org.mockito.Mockito.verify;

@ExtendWith(MockitoExtension.class)
class CanvasControllerTest {

    private MockMvc mockMvc;
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Mock
    private CanvasService canvasService;

    @Mock
    private CanvasResourceService canvasResourceService;

    @InjectMocks
    private CanvasController canvasController;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(canvasController)
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    @DisplayName("JSON 바디로 캔버스 생성 API 성공 시 201 Created")
    void createCanvasJson_Success() throws Exception {
        // given
        Map<String, Object> body = Map.of(
                "canvasName", "New Canvas",
                "description", "My description"
        );
        CanvasSummaryResponse response = new CanvasSummaryResponse(
                100, "/api/canvases/100/image", "My description", "New Canvas", 1
        );
        given(canvasService.createCanvas(eq("New Canvas"), eq("My description"), any(), any(), any())).willReturn(response);

        // when & then
        mockMvc.perform(post("/api/canvases")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(body)))
                .andExpect(status().isCreated())
                .andExpect(jsonPath("$.canvas_id").value(100))
                .andExpect(jsonPath("$.canvas_name").value("New Canvas"))
                .andExpect(jsonPath("$.description").value("My description"))
                .andExpect(jsonPath("$.user_count").value(1));
    }

    @Test
    @DisplayName("캔버스 목록/검색 API 성공 시 200 OK")
    void searchCanvases_Success() throws Exception {
        // given
        CanvasSummaryResponse r1 = new CanvasSummaryResponse(1, "/image1", "desc1", "Canvas 1", 2);
        CanvasSummaryResponse r2 = new CanvasSummaryResponse(2, "/image2", "desc2", "Canvas 2", 5);
        given(canvasService.searchCanvases(eq("Canvas"), any())).willReturn(List.of(r1, r2));

        // when & then
        mockMvc.perform(get("/api/canvases")
                        .param("name", "Canvas"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.length()").value(2))
                .andExpect(jsonPath("$[0].canvas_name").value("Canvas 1"))
                .andExpect(jsonPath("$[1].canvas_name").value("Canvas 2"));
    }

    @Test
    @DisplayName("캔버스 단건 조회 API 성공 시 200 OK")
    void getCanvas_Success() throws Exception {
        // given
        CanvasSummaryResponse r1 = new CanvasSummaryResponse(10, "/image10", "desc10", "Canvas 10", 3);
        given(canvasService.getCanvasSummary(eq(10), any())).willReturn(r1);

        // when & then
        mockMvc.perform(get("/api/canvases/10"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.canvas_id").value(10))
                .andExpect(jsonPath("$.canvas_name").value("Canvas 10"))
                .andExpect(jsonPath("$.description").value("desc10"))
                .andExpect(jsonPath("$.user_count").value(3));
    }

    @Test
    @DisplayName("캔버스 삭제 API 성공 시 204 No Content")
    void deleteCanvas_Success() throws Exception {
        // when & then
        mockMvc.perform(delete("/api/canvases/10"))
                .andExpect(status().isNoContent());
    }

    @Test
    @DisplayName("닉네임과 태그 번호로 참여자를 추가한다")
    void addParticipant_UsesNicknameAndTag() throws Exception {
        mockMvc.perform(post("/api/canvases/10/people")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"nickname\":\"동료\",\"tag_number\":7}"))
                .andExpect(status().isNoContent());
        verify(canvasService).addCanvasParticipant(eq(10), eq("동료"), eq(7), any());
    }

    @Test
    @DisplayName("참여자 제외도 사용자 ID 없이 닉네임과 태그 번호를 사용한다")
    void removeParticipant_UsesNicknameAndTag() throws Exception {
        mockMvc.perform(delete("/api/canvases/10/people")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"nickname\":\"동료\",\"tag_number\":7}"))
                .andExpect(status().isNoContent());
        verify(canvasService).removeCanvasParticipant(eq(10), eq("동료"), eq(7), any());
    }

    @Test
    @DisplayName("캔버스 접속 비밀번호를 서비스 검증 경로로 전달한다")
    void accessCanvas_ForwardsPassword() throws Exception {
        given(canvasService.accessCanvas(eq(10), any(), any(), eq("secret")))
                .willReturn(new com.endpoint.frelog.domain.canvas.dto.CanvasUpdateDtos.AccessResponse(1, "8002", "token"));
        mockMvc.perform(post("/api/canvases/10/access")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"password\":\"secret\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.canvas_access_token").value("token"));
        verify(canvasService).accessCanvas(eq(10), any(), any(), eq("secret"));
    }
}
