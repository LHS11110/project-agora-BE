package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CreateCanvasRequest;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.util.List;

@Service
public class CanvasService {

    private final CanvasInfoRepository canvasInfoRepository;
    private final UserRepository userRepository;

    public CanvasService(CanvasInfoRepository canvasInfoRepository, UserRepository userRepository) {
        this.canvasInfoRepository = canvasInfoRepository;
        this.userRepository = userRepository;
    }

    /**
     * 캔버스 생성 및 초기 정보 등록
     * 요구사항:
     * - user_id 외래키 연동 (소유자 어드민 계정)
     * - 처음에 cache에 redis 및 server는 항상 none(null)으로 시작하고 is_cached는 false로 등록
     */
    @Transactional
    public CanvasResponse createCanvas(CreateCanvasRequest request, Long fallbackUserId) {
        if (canvasInfoRepository.existsByCanvasName(request.canvasName())) {
            throw new CustomException(ErrorCode.CANVAS_ALREADY_EXISTS, "이미 존재하는 캔버스 이름입니다: " + request.canvasName());
        }

        Long resolvedUserId = request.userId() != null ? request.userId() : fallbackUserId;
        if (resolvedUserId == null) {
            // 소유자 ID가 지정되지 않은 경우 첫 번째 사용자 또는 에러
            resolvedUserId = userRepository.findAll().stream()
                    .map(User::getUserId)
                    .findFirst()
                    .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "등록된 사용자가 없습니다. 먼저 계정을 생성해주세요."));
        }

        final Long targetUserId = resolvedUserId;
        User owner = userRepository.findById(targetUserId)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "캔버스 소유자를 찾을 수 없습니다. (user_id: " + targetUserId + ")"));

        Integer targetId = request.canvasId();
        if (targetId == null) {
            Integer maxId = canvasInfoRepository.findMaxCanvasId();
            targetId = (maxId == null ? 0 : maxId) + 1;
        } else {
            if (canvasInfoRepository.existsById(targetId)) {
                throw new CustomException(ErrorCode.CANVAS_ALREADY_EXISTS, "이미 존재하는 캔버스 ID입니다: " + targetId);
            }
        }

        // 항상 redis와 server는 none(null), is_cached는 false로 초기화
        CanvasInfo canvas = new CanvasInfo(targetId, request.canvasName(), owner);
        canvas.setRedisIp(null);
        canvas.setRedisPort(null);
        canvas.setServerIp(null);
        canvas.setServerPort(null);
        canvas.setIsCached(false);

        CanvasInfo saved = canvasInfoRepository.save(canvas);
        return CanvasResponse.from(saved);
    }

    @Transactional
    public CanvasResponse createCanvas(CreateCanvasRequest request) {
        return createCanvas(request, null);
    }

    @Transactional(readOnly = true)
    public CanvasResponse getCanvas(Integer canvasId) {
        CanvasInfo canvas = canvasInfoRepository.findById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));
        return CanvasResponse.from(canvas);
    }

    @Transactional(readOnly = true)
    public CanvasResponse getCanvasByName(String canvasName) {
        CanvasInfo canvas = canvasInfoRepository.findByCanvasName(canvasName)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasName));
        return CanvasResponse.from(canvas);
    }

    @Transactional(readOnly = true)
    public List<CanvasResponse> listCanvases() {
        return canvasInfoRepository.findAll().stream()
                .map(CanvasResponse::from)
                .toList();
    }

    @Transactional(readOnly = true)
    public List<CanvasResponse> listCanvasesByUserId(Long userId) {
        return canvasInfoRepository.findByUser_UserId(userId).stream()
                .map(CanvasResponse::from)
                .toList();
    }

    @Transactional
    public CanvasResponse updateCanvasCache(Integer canvasId, UpdateCanvasCacheRequest request, CustomUserDetails currentUser) {
        CanvasInfo canvas = canvasInfoRepository.findById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));

        validateCanvasOwnerOrAdmin(canvas, currentUser);

        canvas.updateCacheState(
                request.isCached(),
                request.redisIp(),
                request.redisPort(),
                request.serverIp(),
                request.serverPort()
        );

        CanvasInfo updated = canvasInfoRepository.save(canvas);
        return CanvasResponse.from(updated);
    }

    @Transactional
    public CanvasResponse updateCanvasCache(Integer canvasId, UpdateCanvasCacheRequest request) {
        return updateCanvasCache(canvasId, request, null);
    }

    @Transactional
    public void deleteCanvas(Integer canvasId, CustomUserDetails currentUser) {
        CanvasInfo canvas = canvasInfoRepository.findById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));

        validateCanvasOwnerOrAdmin(canvas, currentUser);

        canvasInfoRepository.delete(canvas);
    }

    @Transactional
    public void deleteCanvas(Integer canvasId) {
        deleteCanvas(canvasId, null);
    }

    /**
     * 캔버스 수정/삭제 권한 검증:
     * - 캔버스 소유자(user_id 일치)이거나
     * - 관리자(ROLE_ADMIN)인 경우에만 허용
     */
    private void validateCanvasOwnerOrAdmin(CanvasInfo canvas, CustomUserDetails currentUser) {
        if (currentUser == null || currentUser.getUser() == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        Long currentUserId = currentUser.getUserId();
        boolean isOwner = canvas.getUser() != null && canvas.getUser().getUserId().equals(currentUserId);
        boolean isAdmin = currentUser.getUser().getRole() == com.endpoint.frelog.domain.user.entity.Role.ROLE_ADMIN
                || currentUser.getAuthorities().stream().anyMatch(a -> a.getAuthority().equals("ROLE_ADMIN"));

        if (!isOwner && !isAdmin) {
            throw new CustomException(ErrorCode.ACCESS_DENIED, "해당 캔버스를 수정 또는 삭제할 권한이 없습니다. (소유자 또는 관리자 전용)");
        }
    }
}
