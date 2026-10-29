package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.junit.jupiter.api.Test;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class CanvasRuntimeControllerTest {
    private final CanvasService canvases = mock(CanvasService.class);
    private final CanvasRuntimeService runtime = mock(CanvasRuntimeService.class);
    private final JwtTokenProvider tokens = mock(JwtTokenProvider.class);
    private final CanvasRuntimeController controller = new CanvasRuntimeController(canvases, runtime, tokens);
    private final CustomUserDetails user = mock(CustomUserDetails.class);

    @Test void protectedCanvasRequiresCurrentPasswordGrantBeforeExecution() {
        CanvasDocument doc = mock(CanvasDocument.class);
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getSettingsRevision()).thenReturn(4L);
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12, user)).thenReturn(doc);
        var request = new CanvasRuntimeController.ExecuteRequest("python", "print(1)", "grant");
        assertThrows(CustomException.class, () -> controller.execute(12, request, user));
        verifyNoInteractions(runtime);
        when(tokens.validateCanvasPasswordGrant("grant", 7L, 12, 4L)).thenReturn(true);
        when(runtime.execute(12, "python", "print(1)")).thenReturn(Map.of("output", "1\n"));
        assertEquals("1\n", controller.execute(12, request, user).getBody().get("output"));
    }

    @Test void rejectedMembershipNeverReachesRuntime() {
        when(canvases.getCanvasDocument(12, user)).thenThrow(new CustomException(com.endpoint.frelog.global.exception.ErrorCode.CANVAS_NOT_FOUND));
        assertThrows(CustomException.class, () -> controller.execute(12,
                new CanvasRuntimeController.ExecuteRequest("python", "", null), user));
        verifyNoInteractions(runtime);
    }
}
