package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CreateCanvasRequest;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.entity.CanvasCache;
import com.endpoint.frelog.domain.canvas.repository.CanvasCacheRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.util.List;

@Service
public class CanvasService {

    private final CanvasCacheRepository canvasCacheRepository;

    public CanvasService(CanvasCacheRepository canvasCacheRepository) {
        this.canvasCacheRepository = canvasCacheRepository;
    }

    /**
     * 캔버스 생성 및 캐시 초기 등록
     * 요구사항: 처음에 cache에 redis 및 server는 항상 none(null)으로 시작하고 is_cached는 false로 등록
     */
    @Transactional
    public CanvasResponse createCanvas(CreateCanvasRequest request) {
        if (canvasCacheRepository.existsByCanvasName(request.canvasName())) {
            throw new CustomException(ErrorCode.CANVAS_ALREADY_EXISTS, "이미 존재하는 캔버스 이름입니다: " + request.canvasName());
        }

        Integer targetId = request.canvasId();
        if (targetId == null) {
            Integer maxId = canvasCacheRepository.findMaxCanvasId();
            targetId = (maxId == null ? 0 : maxId) + 1;
        } else {
            if (canvasCacheRepository.existsById(targetId)) {
                throw new CustomException(ErrorCode.CANVAS_ALREADY_EXISTS, "이미 존재하는 캔버스 ID입니다: " + targetId);
            }
        }

        // 항상 redis와 server는 none(null), is_cached는 false로 초기화
        CanvasCache canvas = new CanvasCache(targetId, request.canvasName());
        canvas.setRedisIp(null);
        canvas.setRedisPort(null);
        canvas.setServerIp(null);
        canvas.setServerPort(null);
        canvas.setIsCached(false);

        CanvasCache saved = canvasCacheRepository.save(canvas);
        return CanvasResponse.from(saved);
    }

    @Transactional(readOnly = true)
    public CanvasResponse getCanvas(Integer canvasId) {
        CanvasCache canvas = canvasCacheRepository.findById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));
        return CanvasResponse.from(canvas);
    }

    @Transactional(readOnly = true)
    public CanvasResponse getCanvasByName(String canvasName) {
        CanvasCache canvas = canvasCacheRepository.findByCanvasName(canvasName)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasName));
        return CanvasResponse.from(canvas);
    }

    @Transactional(readOnly = true)
    public List<CanvasResponse> listCanvases() {
        return canvasCacheRepository.findAll().stream()
                .map(CanvasResponse::from)
                .toList();
    }

    @Transactional
    public CanvasResponse updateCanvasCache(Integer canvasId, UpdateCanvasCacheRequest request) {
        CanvasCache canvas = canvasCacheRepository.findById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));

        canvas.updateCacheState(
                request.isCached(),
                request.redisIp(),
                request.redisPort(),
                request.serverIp(),
                request.serverPort()
        );

        CanvasCache updated = canvasCacheRepository.save(canvas);
        return CanvasResponse.from(updated);
    }

    @Transactional
    public void deleteCanvas(Integer canvasId) {
        if (!canvasCacheRepository.existsById(canvasId)) {
            throw new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId);
        }
        canvasCacheRepository.deleteById(canvasId);
    }
}
