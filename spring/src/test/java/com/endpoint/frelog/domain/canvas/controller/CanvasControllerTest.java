package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CreateCanvasRequest;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
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
import java.util.List;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.BDDMockito.given;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.delete;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.patch;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class CanvasControllerTest {

    private MockMvc mockMvc;
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Mock
    private CanvasService canvasService;

    @InjectMocks
    private CanvasController canvasController;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(canvasController)
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    @DisplayName("캔버스 생성 API 성공 시 201 Created 및 userId, 초기 none/false 캐시 응답")
    void createCanvasApi_Success() throws Exception {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("New Canvas", 100, 1L);
        CanvasResponse response = new CanvasResponse(
                100, "New Canvas", 1L, "아고라유저", null, null, null, null, false, LocalDateTime.now(), LocalDateTime.now()
        );
        given(canvasService.createCanvas(any(CreateCanvasRequest.class), any())).willReturn(response);

        // when & then
        mockMvc.perform(post("/api/canvases")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(request)))
                .andExpect(status().isCreated())
                .andExpect(jsonPath("$.canvasId").value(100))
                .andExpect(jsonPath("$.canvasName").value("New Canvas"))
                .andExpect(jsonPath("$.userId").value(1))
                .andExpect(jsonPath("$.userNickname").value("아고라유저"))
                .andExpect(jsonPath("$.redisIp").doesNotExist())
                .andExpect(jsonPath("$.serverIp").doesNotExist())
                .andExpect(jsonPath("$.isCached").value(false));
    }

    @Test
    @DisplayName("캔버스 목록 조회 API 성공 시 200 OK")
    void listCanvasesApi_Success() throws Exception {
        // given
        CanvasResponse response1 = new CanvasResponse(1, "C1", 1L, "아고라유저", null, null, null, null, false, LocalDateTime.now(), LocalDateTime.now());
        CanvasResponse response2 = new CanvasResponse(2, "C2", 2L, "관리자", "127.0.0.1", "6379", "127.0.0.1", "8000", true, LocalDateTime.now(), LocalDateTime.now());
        given(canvasService.listCanvases()).willReturn(List.of(response1, response2));

        // when & then
        mockMvc.perform(get("/api/canvases"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.length()").value(2))
                .andExpect(jsonPath("$[0].canvasName").value("C1"))
                .andExpect(jsonPath("$[0].userId").value(1))
                .andExpect(jsonPath("$[1].isCached").value(true));
    }

    @Test
    @DisplayName("캔버스 단건 조회 API 성공 시 200 OK")
    void getCanvasApi_Success() throws Exception {
        // given
        CanvasResponse response = new CanvasResponse(1, "C1", 1L, "아고라유저", null, null, null, null, false, LocalDateTime.now(), LocalDateTime.now());
        given(canvasService.getCanvas(1)).willReturn(response);

        // when & then
        mockMvc.perform(get("/api/canvases/1"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.canvasId").value(1))
                .andExpect(jsonPath("$.canvasName").value("C1"))
                .andExpect(jsonPath("$.userId").value(1));
    }

    @Test
    @DisplayName("존재하지 않는 캔버스 조회 시 404 NOT_FOUND")
    void getCanvasApi_NotFound() throws Exception {
        given(canvasService.getCanvas(999)).willThrow(new CustomException(ErrorCode.CANVAS_NOT_FOUND));

        mockMvc.perform(get("/api/canvases/999"))
                .andExpect(status().isNotFound())
                .andExpect(jsonPath("$.code").value("CANVAS_001"));
    }

    @Test
    @DisplayName("캔버스 캐시 상태 수정 API 성공 시 200 OK")
    void updateCanvasCacheApi_Success() throws Exception {
        // given
        UpdateCanvasCacheRequest request = new UpdateCanvasCacheRequest(true, "127.0.0.1", "6379", "127.0.0.1", "8000");
        CanvasResponse response = new CanvasResponse(1, "C1", 1L, "아고라유저", "127.0.0.1", "6379", "127.0.0.1", "8000", true, LocalDateTime.now(), LocalDateTime.now());
        given(canvasService.updateCanvasCache(eq(1), any(UpdateCanvasCacheRequest.class), any())).willReturn(response);

        // when & then
        mockMvc.perform(patch("/api/canvases/1/cache")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(request)))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.isCached").value(true))
                .andExpect(jsonPath("$.userId").value(1))
                .andExpect(jsonPath("$.redisIp").value("127.0.0.1"));
    }

    @Test
    @DisplayName("권한 없는 계정이 캔버스 수정 시 403 FORBIDDEN")
    void updateCanvasCacheApi_AccessDenied() throws Exception {
        // given
        UpdateCanvasCacheRequest request = new UpdateCanvasCacheRequest(true, "127.0.0.1", "6379", null, null);
        given(canvasService.updateCanvasCache(eq(1), any(UpdateCanvasCacheRequest.class), any()))
                .willThrow(new CustomException(ErrorCode.ACCESS_DENIED));

        // when & then
        mockMvc.perform(patch("/api/canvases/1/cache")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content(objectMapper.writeValueAsString(request)))
                .andExpect(status().isForbidden())
                .andExpect(jsonPath("$.code").value("AUTH_006"));
    }

    @Test
    @DisplayName("캔버스 삭제 API 성공 시 204 No Content")
    void deleteCanvasApi_Success() throws Exception {
        mockMvc.perform(delete("/api/canvases/1"))
                .andExpect(status().isNoContent());
    }

    @Test
    @DisplayName("권한 없는 계정이 캔버스 삭제 시 403 FORBIDDEN")
    void deleteCanvasApi_AccessDenied() throws Exception {
        org.mockito.BDDMockito.willThrow(new CustomException(ErrorCode.ACCESS_DENIED))
                .given(canvasService).deleteCanvas(eq(1), any());

        mockMvc.perform(delete("/api/canvases/1"))
                .andExpect(status().isForbidden())
                .andExpect(jsonPath("$.code").value("AUTH_006"));
    }
}
