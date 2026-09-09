package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CreateCanvasRequest;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.validation.Valid;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;

@RestController
@RequestMapping("/api/canvases")
public class CanvasController {

    private final CanvasService canvasService;

    public CanvasController(CanvasService canvasService) {
        this.canvasService = canvasService;
    }

    @PostMapping
    public ResponseEntity<CanvasResponse> createCanvas(
            @Valid @RequestBody CreateCanvasRequest request,
            @AuthenticationPrincipal CustomUserDetails userDetails) {
        Long fallbackUserId = (userDetails != null && userDetails.getUser() != null) ? userDetails.getUserId() : null;
        CanvasResponse response = canvasService.createCanvas(request, fallbackUserId);
        return ResponseEntity.status(HttpStatus.CREATED).body(response);
    }

    @GetMapping
    public ResponseEntity<List<CanvasResponse>> listCanvases() {
        List<CanvasResponse> response = canvasService.listCanvases();
        return ResponseEntity.ok(response);
    }

    @GetMapping("/{canvasId}")
    public ResponseEntity<CanvasResponse> getCanvas(@PathVariable Integer canvasId) {
        CanvasResponse response = canvasService.getCanvas(canvasId);
        return ResponseEntity.ok(response);
    }

    @GetMapping("/name/{canvasName}")
    public ResponseEntity<CanvasResponse> getCanvasByName(@PathVariable String canvasName) {
        CanvasResponse response = canvasService.getCanvasByName(canvasName);
        return ResponseEntity.ok(response);
    }

    @GetMapping("/user/{userId}")
    public ResponseEntity<List<CanvasResponse>> listCanvasesByUser(@PathVariable Long userId) {
        List<CanvasResponse> response = canvasService.listCanvasesByUserId(userId);
        return ResponseEntity.ok(response);
    }

    @PatchMapping("/{canvasId}/cache")
    public ResponseEntity<CanvasResponse> updateCanvasCache(
            @PathVariable Integer canvasId,
            @RequestBody UpdateCanvasCacheRequest request,
            @AuthenticationPrincipal CustomUserDetails userDetails) {
        CanvasResponse response = canvasService.updateCanvasCache(canvasId, request, userDetails);
        return ResponseEntity.ok(response);
    }

    @DeleteMapping("/{canvasId}")
    public ResponseEntity<Void> deleteCanvas(
            @PathVariable Integer canvasId,
            @AuthenticationPrincipal CustomUserDetails userDetails) {
        canvasService.deleteCanvas(canvasId, userDetails);
        return ResponseEntity.noContent().build();
    }
}
