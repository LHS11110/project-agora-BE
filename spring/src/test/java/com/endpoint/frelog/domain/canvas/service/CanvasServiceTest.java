package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CreateCanvasRequest;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.entity.CanvasCache;
import com.endpoint.frelog.domain.canvas.repository.CanvasCacheRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;

import java.util.List;
import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.verify;

@ExtendWith(MockitoExtension.class)
class CanvasServiceTest {

    @Mock
    private CanvasCacheRepository canvasCacheRepository;

    @InjectMocks
    private CanvasService canvasService;

    @Test
    @DisplayName("캔버스 생성 시 redis와 server는 항상 none(null), is_cached는 false로 초기화")
    void createCanvas_InitialCacheState_NoneAndFalse() {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("Agora Shared Canvas", 1001);
        given(canvasCacheRepository.existsByCanvasName(request.canvasName())).willReturn(false);
        given(canvasCacheRepository.existsById(1001)).willReturn(false);

        CanvasCache savedEntity = new CanvasCache(1001, "Agora Shared Canvas");
        savedEntity.setRedisIp(null);
        savedEntity.setRedisPort(null);
        savedEntity.setServerIp(null);
        savedEntity.setServerPort(null);
        savedEntity.setIsCached(false);

        given(canvasCacheRepository.save(any(CanvasCache.class))).willReturn(savedEntity);

        // when
        CanvasResponse response = canvasService.createCanvas(request);

        // then
        assertThat(response).isNotNull();
        assertThat(response.canvasId()).isEqualTo(1001);
        assertThat(response.canvasName()).isEqualTo("Agora Shared Canvas");
        assertThat(response.redisIp()).isNull();
        assertThat(response.redisPort()).isNull();
        assertThat(response.serverIp()).isNull();
        assertThat(response.serverPort()).isNull();
        assertThat(response.isCached()).isFalse();

        verify(canvasCacheRepository).save(any(CanvasCache.class));
    }

    @Test
    @DisplayName("canvasId 미입력 시 max(canvas_id) + 1로 자동 채번")
    void createCanvas_AutoGenerateId() {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("Auto Id Canvas", null);
        given(canvasCacheRepository.existsByCanvasName(request.canvasName())).willReturn(false);
        given(canvasCacheRepository.findMaxCanvasId()).willReturn(10);

        CanvasCache savedEntity = new CanvasCache(11, "Auto Id Canvas");
        given(canvasCacheRepository.save(any(CanvasCache.class))).willReturn(savedEntity);

        // when
        CanvasResponse response = canvasService.createCanvas(request);

        // then
        assertThat(response.canvasId()).isEqualTo(11);
        assertThat(response.canvasName()).isEqualTo("Auto Id Canvas");
        assertThat(response.isCached()).isFalse();
    }

    @Test
    @DisplayName("중복된 캔버스 이름으로 생성 시 예외 발생")
    void createCanvas_DuplicateName_ThrowsException() {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("Duplicate Canvas", null);
        given(canvasCacheRepository.existsByCanvasName(request.canvasName())).willReturn(true);

        // when & then
        assertThatThrownBy(() -> canvasService.createCanvas(request))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_ALREADY_EXISTS);
    }

    @Test
    @DisplayName("캔버스 단건 조회 성공")
    void getCanvas_Success() {
        // given
        CanvasCache entity = new CanvasCache(500, "Test Canvas");
        given(canvasCacheRepository.findById(500)).willReturn(Optional.of(entity));

        // when
        CanvasResponse response = canvasService.getCanvas(500);

        // then
        assertThat(response.canvasId()).isEqualTo(500);
        assertThat(response.canvasName()).isEqualTo("Test Canvas");
    }

    @Test
    @DisplayName("존재하지 않는 캔버스 조회 시 예외 발생")
    void getCanvas_NotFound_ThrowsException() {
        // given
        given(canvasCacheRepository.findById(999)).willReturn(Optional.empty());

        // when & then
        assertThatThrownBy(() -> canvasService.getCanvas(999))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_NOT_FOUND);
    }

    @Test
    @DisplayName("전체 캔버스 목록 조회")
    void listCanvases_Success() {
        // given
        CanvasCache c1 = new CanvasCache(1, "Canvas 1");
        CanvasCache c2 = new CanvasCache(2, "Canvas 2");
        given(canvasCacheRepository.findAll()).willReturn(List.of(c1, c2));

        // when
        List<CanvasResponse> list = canvasService.listCanvases();

        // then
        assertThat(list).hasSize(2);
        assertThat(list.get(0).canvasName()).isEqualTo("Canvas 1");
        assertThat(list.get(1).canvasName()).isEqualTo("Canvas 2");
    }

    @Test
    @DisplayName("캔버스 캐시 상태 및 Redis/Server 정보 업데이트 성공")
    void updateCanvasCache_Success() {
        // given
        CanvasCache entity = new CanvasCache(100, "Cached Canvas");
        given(canvasCacheRepository.findById(100)).willReturn(Optional.of(entity));
        given(canvasCacheRepository.save(any(CanvasCache.class))).willAnswer(invocation -> invocation.getArgument(0));

        UpdateCanvasCacheRequest updateReq = new UpdateCanvasCacheRequest(
                true, "127.0.0.1", "6379", "127.0.0.1", "8000"
        );

        // when
        CanvasResponse response = canvasService.updateCanvasCache(100, updateReq);

        // then
        assertThat(response.isCached()).isTrue();
        assertThat(response.redisIp()).isEqualTo("127.0.0.1");
        assertThat(response.redisPort()).isEqualTo("6379");
        assertThat(response.serverIp()).isEqualTo("127.0.0.1");
        assertThat(response.serverPort()).isEqualTo("8000");
    }

    @Test
    @DisplayName("캔버스 삭제 성공")
    void deleteCanvas_Success() {
        // given
        given(canvasCacheRepository.existsById(100)).willReturn(true);

        // when
        canvasService.deleteCanvas(100);

        // then
        verify(canvasCacheRepository).deleteById(100);
    }
}
