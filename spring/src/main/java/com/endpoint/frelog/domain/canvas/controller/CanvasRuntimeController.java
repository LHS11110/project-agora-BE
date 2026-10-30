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
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestHeader;
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

    public record ResetRequest(String passwordGrant) {}
    public record ConfigureRequest(String language, Map<String, Object> config, Long revision, String passwordGrant) {}

    @GetMapping("/{canvasId}/runtime/settings")
    public ResponseEntity<Map<String, Object>> settings(@PathVariable Integer canvasId,
            @RequestHeader(value="X-Canvas-Password-Grant", required=false) String passwordGrant,
            @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, passwordGrant, user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(runtime.settings(canvasId));
    }

    @PutMapping("/{canvasId}/runtime/settings")
    public ResponseEntity<Map<String, Object>> configure(@PathVariable Integer canvasId,
            @RequestBody ConfigureRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.configure(canvasId, request.language(), request.config(), request.revision()));
    }


    public record ResourcesRequest(String template, Long revision, String passwordGrant) {}

    @PutMapping("/{canvasId}/runtime/resources")
    public ResponseEntity<Map<String, Object>> resources(@PathVariable Integer canvasId,
            @RequestBody ResourcesRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.resources(canvasId, request.template(), request.revision()));
    }

    public record DiskRequest(Integer sizeGiB, Long revision, String passwordGrant) {}

    @PutMapping("/{canvasId}/runtime/disk")
    public ResponseEntity<Map<String, Object>> disk(@PathVariable Integer canvasId,
            @RequestBody DiskRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.disk(canvasId, request.sizeGiB(), request.revision()));
    }

    public record InstallJavaRequest(String passwordGrant) {}

    @PostMapping("/{canvasId}/runtime/java/dependencies")
    public ResponseEntity<Map<String, Object>> installJavaDependencies(@PathVariable Integer canvasId,
            @RequestBody InstallJavaRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(runtime.installJavaDependencies(canvasId));
    }

    @PostMapping("/{canvasId}/runtime/reset")
    public ResponseEntity<Map<String, Object>> reset(@PathVariable Integer canvasId,
            @RequestBody ResetRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(runtime.reset(canvasId));
    }

    @PostMapping("/{canvasId}/runtime/execute")
    public ResponseEntity<Map<String, Object>> execute(@PathVariable Integer canvasId,
            @RequestBody ExecuteRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.execute(canvasId, request.language(), request.code()));
    }
    private void authorize(Integer canvasId, String passwordGrant, CustomUserDetails user) {
        // The authoritative live canvas document checks current membership or public visibility.
        CanvasDocument doc = canvases.getCanvasDocument(canvasId, user);
        String hash = doc.getCanvasPasswordHash();
        long revision = doc.getSettingsRevision() == null ? 0 : doc.getSettingsRevision();
        if (hash != null && !hash.isBlank() && !tokens.validateCanvasPasswordGrant(
                passwordGrant, user.getUserId(), canvasId, revision)) {
            throw new CustomException(ErrorCode.CANVAS_PASSWORD_REQUIRED);
        }
    }

}
