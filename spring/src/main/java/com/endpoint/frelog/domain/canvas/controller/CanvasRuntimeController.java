package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import com.endpoint.frelog.domain.canvas.service.CanvasGroupPermissions;
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

    public record ExecuteRequest(String language, String code, String passwordGrant, String stdin, String objectId) {
        public ExecuteRequest(String language, String code, String passwordGrant) { this(language, code, passwordGrant, null, null); }
        public ExecuteRequest(String language, String code, String passwordGrant, String stdin) { this(language, code, passwordGrant, stdin, null); }
    }

    public record ResetRequest(String passwordGrant) {}
    public record ConfigureRequest(String language, Map<String, Object> config, Long revision, String passwordGrant) {}

    @GetMapping("/{canvasId}/runtime/settings")
    public ResponseEntity<Map<String, Object>> settings(@PathVariable Integer canvasId,
            @RequestHeader(value="X-Canvas-Password-Grant", required=false) String passwordGrant,
            @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, passwordGrant, user, "read");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(runtime.settings(canvasId));
    }

    @PutMapping("/{canvasId}/runtime/settings")
    public ResponseEntity<Map<String, Object>> configure(@PathVariable Integer canvasId,
            @RequestBody ConfigureRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user, "manage_runtime");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.configure(canvasId, request.language(), request.config(), request.revision()));
    }


    public record ResourcesRequest(String template, Long revision, String passwordGrant) {}

    @PutMapping("/{canvasId}/runtime/resources")
    public ResponseEntity<Map<String, Object>> resources(@PathVariable Integer canvasId,
            @RequestBody ResourcesRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user, "manage_runtime");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.resources(canvasId, request.template(), request.revision()));
    }

    public record DiskRequest(Integer sizeGiB, Long revision, String passwordGrant) {}

    @PutMapping("/{canvasId}/runtime/disk")
    public ResponseEntity<Map<String, Object>> disk(@PathVariable Integer canvasId,
            @RequestBody DiskRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user, "manage_runtime");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.disk(canvasId, request.sizeGiB(), request.revision()));
    }

    public record InstallJavaRequest(String passwordGrant) {}

    @PostMapping("/{canvasId}/runtime/java/dependencies")
    public ResponseEntity<Map<String, Object>> installJavaDependencies(@PathVariable Integer canvasId,
            @RequestBody InstallJavaRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user, "manage_runtime");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(runtime.installJavaDependencies(canvasId));
    }

    @PostMapping("/{canvasId}/runtime/reset")
    public ResponseEntity<Map<String, Object>> reset(@PathVariable Integer canvasId,
            @RequestBody ResetRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorize(canvasId, request.passwordGrant(), user, "manage_runtime");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(runtime.reset(canvasId));
    }

    @PostMapping("/{canvasId}/runtime/execute")
    public ResponseEntity<Map<String, Object>> execute(@PathVariable Integer canvasId,
            @RequestBody ExecuteRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        var document = authorize(canvasId, request.passwordGrant(), user, "execute");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.execute(canvasId, request.language(), request.code(), request.stdin(),
                        user.getUserId(), revision(document), request.objectId()));
    }
    @PostMapping(value="/{canvasId}/runtime/execute/stream", produces="application/x-ndjson")
    public ResponseEntity<org.springframework.web.servlet.mvc.method.annotation.StreamingResponseBody> executeStream(
            @PathVariable Integer canvasId, @RequestBody ExecuteRequest request,
            @AuthenticationPrincipal CustomUserDetails user, jakarta.servlet.http.HttpServletResponse servletResponse) {
        var document = authorize(canvasId, request.passwordGrant(), user, "execute");
        org.springframework.web.servlet.mvc.method.annotation.StreamingResponseBody body = output -> {
            var json = new com.fasterxml.jackson.databind.ObjectMapper();
            java.util.function.Consumer<Map<String,Object>> send = frame -> {
                try { output.write(json.writeValueAsBytes(frame)); output.write('\n'); output.flush(); servletResponse.flushBuffer(); }
                catch (java.io.IOException error) { throw new java.io.UncheckedIOException(error); }
            };
            try {
                var result = runtime.executeStream(canvasId, request.language(), request.code(), request.stdin(),
                        user.getUserId(), revision(document), request.objectId(), send);
                send.accept(Map.of("type", "result", "result", result));
            } catch (java.io.UncheckedIOException disconnected) {
                // The manager drains the guest execution even after this HTTP client leaves.
            } catch (CustomException error) {
                send.accept(Map.of("type", "error", "code", error.getErrorCode().getCode(), "message", error.getMessage()));
            } catch (RuntimeException error) {
                send.accept(Map.of("type", "error", "message", "코드 실행 스트림이 종료되었습니다."));
            }
        };
        return ResponseEntity.ok().contentType(org.springframework.http.MediaType.parseMediaType("application/x-ndjson"))
                .cacheControl(CacheControl.noStore()).header("X-Accel-Buffering", "no").body(body);
    }

    @PostMapping("/{canvasId}/runtime/build")
    public ResponseEntity<Map<String, Object>> build(@PathVariable Integer canvasId,
            @RequestBody ExecuteRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        var document = authorize(canvasId, request.passwordGrant(), user, "execute");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(runtime.build(canvasId, request.language(), request.code(), user.getUserId(),
                        revision(document), request.objectId()));
    }

    public record TerminalRequest(String objectId, String operation, String data, Integer cols, Integer rows, Long offset, String passwordGrant) {}

    @PostMapping("/{canvasId}/runtime/terminal")
    public ResponseEntity<Map<String,Object>> terminal(@PathVariable Integer canvasId,
            @RequestBody TerminalRequest request, @AuthenticationPrincipal CustomUserDetails user) {
        authorizeTerminal(canvasId,request.objectId(),request.passwordGrant(),user);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(runtime.terminal(canvasId,user.getUserId(),
                request.objectId(),request.operation(),request.data(),request.cols(),request.rows(),request.offset()));
    }

    public record NotebookRequest(String objectId,String operation,String code,String passwordGrant) {}
    @PostMapping("/{canvasId}/runtime/notebook")
    public ResponseEntity<Map<String,Object>> notebook(@PathVariable Integer canvasId,@RequestBody NotebookRequest request,@AuthenticationPrincipal CustomUserDetails user) {
        var document=authorizeRuntimeObject(canvasId,request.objectId(),request.passwordGrant(),user,"notebook");
        if("prepare".equals(request.operation())&&!CanvasGroupPermissions.allows(document,user,"write"))throw new CustomException(ErrorCode.ACCESS_DENIED,"실행 환경을 변경할 권한이 없습니다.");
        var result=runtime.notebook(canvasId,request.objectId(),request.operation(),request.code()==null?"":request.code(),
                user.getUserId(),revision(document));
        authorizeRuntimeObject(canvasId,request.objectId(),request.passwordGrant(),user,"notebook");
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(result);
    }

    public void authorizeTerminal(Integer canvasId, String objectId, String passwordGrant, CustomUserDetails user) {
        authorizeTerminal(DocumentScope.CANVAS,canvasId,objectId,passwordGrant,user);
    }

    public void authorizeTerminal(DocumentScope scope, Integer spaceId, String objectId, String passwordGrant, CustomUserDetails user) {
        authorizeRuntimeObject(scope,spaceId,objectId,passwordGrant,user,"terminal");
    }

    private CanvasDocument authorizeRuntimeObject(Integer canvasId, String objectId, String passwordGrant, CustomUserDetails user,String role) {
        return authorizeRuntimeObject(DocumentScope.CANVAS,canvasId,objectId,passwordGrant,user,role);
    }

    private CanvasDocument authorizeRuntimeObject(DocumentScope scope, Integer spaceId, String objectId, String passwordGrant, CustomUserDetails user,String role) {
        if(objectId==null||!objectId.matches("[a-zA-Z0-9_-]{1,128}"))throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"실행 객체를 확인하세요.");
        CanvasDocument doc = authorize(scope,spaceId,passwordGrant,user,"execute",objectId);
        Object raw = doc.getItems().get(objectId);
        if (!(raw instanceof Map<?,?> item) || !"shape".equals(item.get("kind")) || !role.equals(item.get("runtimeRole")))
            throw new CustomException(ErrorCode.ACCESS_DENIED,"실행 객체가 없거나 접근할 수 없습니다.");
        if (!CanvasGroupPermissions.admin(doc,user.getUserId())) {
            var bits = new com.endpoint.frelog.domain.canvas.service.CanvasGroupBits(doc);
            var groups = new java.util.HashSet<String>();
            doc.getInnerGroup().forEach((name,members)->{
                if (((members != null && members.contains(user.getUserId())) || ("public".equals(doc.getVisibility()) && name.equals(doc.getInitGroup())))
                        && CanvasGroupPermissions.group(doc,name,"read") && CanvasGroupPermissions.group(doc,name,"execute")) groups.add(name);
            });
            if ((bits.permissionMask(item.get("permission")) & bits.mask(groups)) == 0)
                throw new CustomException(ErrorCode.ACCESS_DENIED,"실행 객체의 실행 권한이 없습니다.");
        }
        return doc;
    }

    private CanvasDocument authorize(Integer canvasId, String passwordGrant, CustomUserDetails user, String capability) {
        return authorize(DocumentScope.CANVAS,canvasId,passwordGrant,user,capability,null);
    }

    private static long revision(CanvasDocument document) {
        return document.getSettingsRevision() == null ? 0L : document.getSettingsRevision();
    }
    public CanvasDocument authorize(Integer canvasId, String passwordGrant, CustomUserDetails user, String capability, String itemId) {
        return authorize(DocumentScope.CANVAS,canvasId,passwordGrant,user,capability,itemId);
    }

    public CanvasDocument authorize(DocumentScope scope, Integer spaceId, String passwordGrant, CustomUserDetails user, String capability, String itemId) {
        // The authoritative live canvas/workspace document checks current membership or public visibility.
        CanvasDocument doc = scope == DocumentScope.CANVAS
                ? (itemId == null ? canvases.getCanvasDocument(spaceId, user) : canvases.getCanvasItemDocument(spaceId, user, itemId))
                : canvases.getItemDocument(scope, spaceId, user, itemId);
        boolean allowed="manage_runtime".equals(capability)
                ? CanvasGroupPermissions.allows(doc,user,"write")&&CanvasGroupPermissions.allows(doc,user,"execute")
                : CanvasGroupPermissions.allows(doc,user,capability);
        if(!allowed)throw new CustomException(ErrorCode.ACCESS_DENIED,"이 그룹에는 해당 작업의 권한이 없습니다.");
        String hash = doc.getCanvasPasswordHash();
        long revision = doc.getPasswordRevision();
        if ("public".equals(doc.getVisibility()) && hash != null && !hash.isBlank()) {
            boolean validGrant = scope == DocumentScope.CANVAS
                    ? tokens.validateCanvasPasswordGrant(passwordGrant, user.getUserId(), spaceId, revision)
                    : tokens.validatePasswordGrant(scope, passwordGrant, user.getUserId(), spaceId, revision);
            if (!validGrant) {
                throw new CustomException(scope.passwordRequiredError());
            }
        }
        return doc;
    }

}
