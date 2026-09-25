package com.endpoint.frelog.global.controller;

import com.endpoint.frelog.domain.user.entity.UserSession;
import com.endpoint.frelog.domain.user.repository.UserSessionRepository;
import org.springframework.http.ResponseEntity;
import org.springframework.stereotype.Controller;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.ResponseBody;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

@Controller
public class TestPageController {

    private final UserSessionRepository userSessionRepository;

    public TestPageController(UserSessionRepository userSessionRepository) {
        this.userSessionRepository = userSessionRepository;
    }

    /**
     * Compatibility endpoint for the testbed. Active-canvas state is read from
     * Spring's database session rows; this endpoint no longer proxies to C++.
     */
    @GetMapping("/api/test/cpp-active-canvases")
    @ResponseBody
    @Transactional(readOnly = true)
    public ResponseEntity<?> activeCanvases() {
        Map<Integer, Integer> activeUsersByCanvas = new TreeMap<>();
        for (UserSession session : userSessionRepository.findByIsAccessedTrue()) {
            if (session.getCanvas() == null || session.getCanvas().getCanvasId() == null) continue;
            activeUsersByCanvas.merge(session.getCanvas().getCanvasId(), 1, Integer::sum);
        }

        List<Map<String, Object>> canvases = new ArrayList<>();
        activeUsersByCanvas.forEach((canvasId, userCount) -> canvases.add(Map.of(
                "canvas_id", canvasId,
                "active_user_count", userCount
        )));
        return ResponseEntity.ok(Map.of(
                "status", "success",
                "count", canvases.size(),
                "canvases", canvases
        ));
    }

    /** Compatibility endpoint; reports the DB-backed number of active canvases. */
    @GetMapping("/api/test/cpp-canvas-count")
    @ResponseBody
    @Transactional(readOnly = true)
    public ResponseEntity<?> activeCanvasCount() {
        long count = userSessionRepository.findByIsAccessedTrue().stream()
                .map(UserSession::getCanvas)
                .filter(canvas -> canvas != null && canvas.getCanvasId() != null)
                .map(canvas -> canvas.getCanvasId())
                .distinct()
                .count();
        return ResponseEntity.ok(Map.of("status", "success", "count", count));
    }
}
