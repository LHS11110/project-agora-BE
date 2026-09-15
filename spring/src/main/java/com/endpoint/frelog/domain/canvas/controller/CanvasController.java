package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CanvasSummaryResponse;
import com.endpoint.frelog.domain.canvas.dto.CanvasUpdateDtos;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.service.CanvasResourceService;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.validation.Valid;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RequestPart;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.multipart.MultipartFile;

import java.nio.file.Path;
import java.util.List;

@RestController
@RequestMapping("/api/canvases")
public class CanvasController {

    private final CanvasService canvasService;
    private final CanvasResourceService canvasResourceService;

    public CanvasController(CanvasService canvasService, CanvasResourceService canvasResourceService) {
        this.canvasService = canvasService;
        this.canvasResourceService = canvasResourceService;
    }

    /**
     * 1. 캔버스 생성 (로그인한 사용자만 허용)
     * Multipart 또는 파라미터로 대표 이미지, 캔버스 이름, 설명, 비밀번호 수신
     */
    @PostMapping(consumes = {MediaType.MULTIPART_FORM_DATA_VALUE, MediaType.APPLICATION_OCTET_STREAM_VALUE})
    public ResponseEntity<CanvasSummaryResponse> createCanvasMultipart(
            @RequestParam("canvasName") String canvasName,
            @RequestParam(value = "description", required = false) String description,
            @RequestParam(value = "canvasPassword", required = false) String canvasPassword,
            @RequestPart(value = "image", required = false) MultipartFile image,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        CanvasSummaryResponse response = canvasService.createCanvas(canvasName, description, canvasPassword, image, currentUser);
        return ResponseEntity.status(HttpStatus.CREATED).body(response);
    }

    @PostMapping(consumes = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<CanvasSummaryResponse> createCanvasJson(
            @RequestBody java.util.Map<String, Object> body,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        String canvasName = (String) body.get("canvasName");
        if (canvasName == null) canvasName = (String) body.get("canvas-name");
        String description = (String) body.get("description");
        String canvasPassword = (String) body.get("canvasPassword");
        if (canvasPassword == null) canvasPassword = (String) body.get("canvas-password");

        CanvasSummaryResponse response = canvasService.createCanvas(canvasName, description, canvasPassword, null, currentUser);
        return ResponseEntity.status(HttpStatus.CREATED).body(response);
    }

    /**
     * 2. 캔버스 검색 (로그인한 사용자만 허용)
     */
    @GetMapping("/search")
    public ResponseEntity<List<CanvasSummaryResponse>> searchCanvases(
            @RequestParam(value = "name", required = false) String name,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.searchCanvases(name, currentUser));
    }

    @GetMapping
    public ResponseEntity<List<CanvasSummaryResponse>> listOrSearchCanvases(
            @RequestParam(value = "name", required = false) String name,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.searchCanvases(name, currentUser));
    }

    /**
     * 3. 캔버스 조회 (로그인한 사용자만 허용)
     */
    @GetMapping("/{canvasId}")
    public ResponseEntity<CanvasSummaryResponse> getCanvas(
            @PathVariable Integer canvasId,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.getCanvasSummary(canvasId, currentUser));
    }

    /**
     * 캔버스 대표 이미지 서빙
     */
    @GetMapping("/{canvasId}/image")
    public ResponseEntity<Resource> getCanvasImage(@PathVariable Integer canvasId) {
        Path imagePath = canvasResourceService.getRepresentativeImageFile(canvasId);
        if (imagePath == null) {
            canvasResourceService.saveDefaultImage(canvasId);
            imagePath = canvasResourceService.getRepresentativeImageFile(canvasId);
        }

        if (imagePath != null) {
            Resource resource = new FileSystemResource(imagePath);
            return ResponseEntity.ok()
                    .header(HttpHeaders.CONTENT_TYPE, MediaType.IMAGE_PNG_VALUE)
                    .body(resource);
        }

        return ResponseEntity.notFound().build();
    }

    /**
     * 캔버스 원본 도큐먼트 조회
     */
    @GetMapping("/{canvasId}/document")
    public ResponseEntity<CanvasDocument> getCanvasDocument(
            @PathVariable Integer canvasId,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.getCanvasDocument(canvasId, currentUser));
    }

    // =========================================================================
    // 4. 캔버스 변경 API (어드민 그룹 또는 시스템 관리자 전용)
    // =========================================================================

    @PatchMapping("/{canvasId}/name")
    public ResponseEntity<CanvasSummaryResponse> updateCanvasName(
            @PathVariable Integer canvasId,
            @Valid @RequestBody CanvasUpdateDtos.UpdateNameRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.updateCanvasName(canvasId, request.canvasName(), currentUser));
    }

    @PatchMapping("/{canvasId}/owner")
    public ResponseEntity<CanvasSummaryResponse> updateCanvasOwner(
            @PathVariable Integer canvasId,
            @Valid @RequestBody CanvasUpdateDtos.UpdateOwnerRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.updateCanvasOwner(canvasId, request.adminUserId(), currentUser));
    }

    @PatchMapping("/{canvasId}/description")
    public ResponseEntity<CanvasSummaryResponse> updateCanvasDescription(
            @PathVariable Integer canvasId,
            @RequestBody CanvasUpdateDtos.UpdateDescriptionRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.updateCanvasDescription(canvasId, request.description(), currentUser));
    }

    @PatchMapping("/{canvasId}/password")
    public ResponseEntity<CanvasSummaryResponse> updateCanvasPassword(
            @PathVariable Integer canvasId,
            @RequestBody CanvasUpdateDtos.UpdatePasswordRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.updateCanvasPassword(canvasId, request.canvasPassword(), currentUser));
    }

    @PostMapping("/{canvasId}/people")
    public ResponseEntity<CanvasDocument> addPeople(
            @PathVariable Integer canvasId,
            @Valid @RequestBody CanvasUpdateDtos.PeopleRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.addPeople(canvasId, request.userId(), currentUser));
    }

    @DeleteMapping("/{canvasId}/people/{userId}")
    public ResponseEntity<CanvasDocument> removePeople(
            @PathVariable Integer canvasId,
            @PathVariable Long userId,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.removePeople(canvasId, userId, currentUser));
    }

    @PostMapping("/{canvasId}/groups")
    public ResponseEntity<CanvasDocument> addInnerGroup(
            @PathVariable Integer canvasId,
            @Valid @RequestBody CanvasUpdateDtos.GroupRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.addInnerGroup(canvasId, request.groupName(), currentUser));
    }

    @DeleteMapping("/{canvasId}/groups/{groupName}")
    public ResponseEntity<CanvasDocument> removeInnerGroup(
            @PathVariable Integer canvasId,
            @PathVariable String groupName,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.removeInnerGroup(canvasId, groupName, currentUser));
    }

    @PostMapping("/{canvasId}/groups/{groupName}/members")
    public ResponseEntity<CanvasDocument> addGroupMember(
            @PathVariable Integer canvasId,
            @PathVariable String groupName,
            @Valid @RequestBody CanvasUpdateDtos.GroupMemberRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.addGroupMember(canvasId, groupName, request.userId(), currentUser));
    }

    @DeleteMapping("/{canvasId}/groups/{groupName}/members/{userId}")
    public ResponseEntity<CanvasDocument> removeGroupMember(
            @PathVariable Integer canvasId,
            @PathVariable String groupName,
            @PathVariable Long userId,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.removeGroupMember(canvasId, groupName, userId, currentUser));
    }

    @PatchMapping("/{canvasId}/init-group")
    public ResponseEntity<CanvasDocument> updateInitGroup(
            @PathVariable Integer canvasId,
            @Valid @RequestBody CanvasUpdateDtos.InitGroupRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.updateInitGroup(canvasId, request.initGroup(), currentUser));
    }

    /**
     * 캔버스 캐시 상태 업데이트 (PATCH /api/canvases/{canvasId}/cache)
     */
    @PatchMapping("/{canvasId}/cache")
    public ResponseEntity<CanvasResponse> updateCanvasCache(
            @PathVariable Integer canvasId,
            @RequestBody UpdateCanvasCacheRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(canvasService.updateCanvasCache(canvasId, request, currentUser));
    }

    /**
     * 5. 캔버스 삭제 (소유자 또는 시스템 관리자 전용)
     */
    @DeleteMapping("/{canvasId}")
    public ResponseEntity<Void> deleteCanvas(
            @PathVariable Integer canvasId,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        canvasService.deleteCanvas(canvasId, currentUser);
        return ResponseEntity.noContent().build();
    }

    /**
     * 6. Access API (별칭 경로: /api/canvases/{canvasId}/access)
     */
    @PostMapping("/{canvasId}/access")
    public ResponseEntity<CanvasUpdateDtos.AccessResponse> accessCanvasPath(
            @PathVariable Integer canvasId,
            HttpServletRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        String authHeader = request.getHeader(HttpHeaders.AUTHORIZATION);
        String token = (authHeader != null && authHeader.startsWith("Bearer ")) ? authHeader.substring(7) : "";
        CanvasUpdateDtos.AccessResponse response = canvasService.accessCanvas(canvasId, token, currentUser);
        return ResponseEntity.ok(response);
    }

    /**
     * 6-1. Disconnect API (별칭 경로: POST /api/canvases/{canvasId}/disconnect)
     */
    @PostMapping("/{canvasId}/disconnect")
    public ResponseEntity<?> disconnectCanvasPath(
            @PathVariable Integer canvasId,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        canvasService.disconnectCanvasAccess(canvasId, currentUser);
        java.util.Map<String, Object> resp = new java.util.HashMap<>();
        resp.put("status", "success");
        resp.put("message", "실시간 소켓/웹소켓 연결이 성공적으로 종료되었습니다.");
        resp.put("canvas_id", canvasId);
        return ResponseEntity.ok(resp);
    }
}
