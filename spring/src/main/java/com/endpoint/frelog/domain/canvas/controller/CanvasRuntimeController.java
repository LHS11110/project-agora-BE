package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.Map;

@RestController
@RequestMapping("/api/canvases")
public class CanvasRuntimeController {
    private final CanvasService canvases;
    private final CanvasRuntimeService runtime;
    private final JwtTokenProvider tokens;

    public CanvasRuntimeController(CanvasService canvases, CanvasRuntimeService runtime, JwtTokenProvider tokens) {
        this.canvases = canvases;
        this.runtime = runtime;
        this.tokens = tokens;
    }

    public record ExecuteRequest(String language, String code, String passwordGrant) {}

    @PostMapping("/{canvasId}/runtime/execute")
    public ResponseEntity<Map<String, Object>> execute(@PathVariable Integer canvasId,
            @RequestBody ExecuteRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        // The authoritative live canvas document checks current membership or public visibility.
        CanvasDocument doc = canvases.getCanvasDocument(canvasId, user);
        String hash = doc.getCanvasPasswordHash();
        long revision = doc.getSettingsRevision() == null ? 0 : doc.getSettingsRevision();
        if (hash != null && !hash.isBlank() && !tokens.validateCanvasPasswordGrant(
                request.passwordGrant(), user.getUserId(), canvasId, revision)) {
            throw new CustomException(ErrorCode.CANVAS_PASSWORD_REQUIRED);
        }
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.execute(canvasId, request.language(), request.code()));
    }
}
