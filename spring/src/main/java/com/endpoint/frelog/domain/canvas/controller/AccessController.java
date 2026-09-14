package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasUpdateDtos;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.validation.Valid;
import org.springframework.http.HttpHeaders;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api/access")
public class AccessController {

    private final CanvasService canvasService;

    public AccessController(CanvasService canvasService) {
        this.canvasService = canvasService;
    }

    /**
     * 5. Access API (POST /api/access)
     * - 로그인한 사용자만 허용, 초대된 사용자 리스트(people)에 속한 경우만 허용
     * - Parameter: canvas_id
     * - C++ 서버 IP 및 Port 반환
     */
    @PostMapping
    public ResponseEntity<CanvasUpdateDtos.AccessResponse> accessCanvas(
            @Valid @RequestBody CanvasUpdateDtos.AccessRequest request,
            HttpServletRequest servletRequest,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        String authHeader = servletRequest.getHeader(HttpHeaders.AUTHORIZATION);
        String token = (authHeader != null && authHeader.startsWith("Bearer ")) ? authHeader.substring(7) : "";
        CanvasUpdateDtos.AccessResponse response = canvasService.accessCanvas(request.canvasId(), token, currentUser);
        return ResponseEntity.ok(response);
    }

    /**
     * 5-1. Disconnect Access API (POST /api/access/disconnect)
     */
    @PostMapping("/disconnect")
    public ResponseEntity<?> disconnectCanvas(
            @RequestBody(required = false) java.util.Map<String, Object> request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        Integer canvasId = null;
        if (request != null && request.containsKey("canvas_id")) {
            try {
                canvasId = Integer.parseInt(request.get("canvas_id").toString());
            } catch (Exception ignored) {}
        }
        canvasService.disconnectCanvasAccess(canvasId, currentUser);
        java.util.Map<String, Object> resp = new java.util.HashMap<>();
        resp.put("status", "success");
        resp.put("message", "실시간 소켓/웹소켓 연결이 성공적으로 종료되었습니다.");
        if (canvasId != null) resp.put("canvas_id", canvasId);
        return ResponseEntity.ok(resp);
    }
}
