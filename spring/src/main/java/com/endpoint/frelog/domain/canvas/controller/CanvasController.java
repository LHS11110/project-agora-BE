package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CreateCanvasRequest;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import jakarta.validation.Valid;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
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
    public ResponseEntity<CanvasResponse> createCanvas(@Valid @RequestBody CreateCanvasRequest request) {
        CanvasResponse response = canvasService.createCanvas(request);
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

    @PatchMapping("/{canvasId}/cache")
    public ResponseEntity<CanvasResponse> updateCanvasCache(
            @PathVariable Integer canvasId,
            @RequestBody UpdateCanvasCacheRequest request) {
        CanvasResponse response = canvasService.updateCanvasCache(canvasId, request);
        return ResponseEntity.ok(response);
    }

    @DeleteMapping("/{canvasId}")
    public ResponseEntity<Void> deleteCanvas(@PathVariable Integer canvasId) {
        canvasService.deleteCanvas(canvasId);
        return ResponseEntity.noContent().build();
    }
}
