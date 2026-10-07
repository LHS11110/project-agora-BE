package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.service.CanvasPdfService;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;
import java.nio.file.Files;
import java.util.Map;

@RestController
@RequestMapping("/api/canvases/{canvasId}/pdfs")
public class CanvasPdfController {
    private final CanvasService canvases;
    private final CanvasPdfService pdfs;
    public CanvasPdfController(CanvasService canvases, CanvasPdfService pdfs) { this.canvases = canvases; this.pdfs = pdfs; }
    @PostMapping(consumes = MediaType.MULTIPART_FORM_DATA_VALUE)
    public ResponseEntity<Map<String, String>> upload(@PathVariable Integer canvasId,
        @RequestPart("file") MultipartFile file, @RequestPart(value = "thumbnail", required = false) MultipartFile thumbnail,
        @AuthenticationPrincipal CustomUserDetails user) {
        canvases.getCanvasSettings(canvasId, user);
        return ResponseEntity.status(HttpStatus.CREATED).body(pdfs.save(canvasId, file, thumbnail));
    }
    @GetMapping("/{id}")
    public ResponseEntity<Resource> read(@PathVariable Integer canvasId, @PathVariable String id, @AuthenticationPrincipal CustomUserDetails user) {
        return resource(canvasId, id, user, false);
    }
    @GetMapping("/{id}/thumbnail")
    public ResponseEntity<Resource> thumbnail(@PathVariable Integer canvasId, @PathVariable String id, @AuthenticationPrincipal CustomUserDetails user) {
        return resource(canvasId, id, user, true);
    }
    private ResponseEntity<Resource> resource(Integer canvasId, String id, CustomUserDetails user, boolean thumbnail) {
        canvases.getCanvasSettings(canvasId, user);
        var path = pdfs.path(canvasId, id, thumbnail);
        if (!Files.isRegularFile(path)) return ResponseEntity.notFound().build();
        return ResponseEntity.ok().contentType(thumbnail ? MediaType.IMAGE_PNG : MediaType.APPLICATION_PDF)
            .header(HttpHeaders.CACHE_CONTROL, "private, no-store").header("X-Content-Type-Options", "nosniff")
            .header(HttpHeaders.CONTENT_DISPOSITION, "inline; filename=\"document" + (thumbnail ? ".png" : ".pdf") + "\"")
            .body(new FileSystemResource(path));
    }
}
