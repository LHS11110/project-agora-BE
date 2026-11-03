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
        when(runtime.execute(12, "python", "input()", "first\nsecond\n")).thenReturn(Map.of("output", "first\n"));
        var withInput = new CanvasRuntimeController.ExecuteRequest("python", "input()", "grant", "first\nsecond\n");
        assertEquals("first\n", controller.execute(12, withInput, user).getBody().get("output"));
        verify(runtime).execute(12, "python", "input()", "first\nsecond\n");
    }

    @Test void rejectedMembershipNeverReachesRuntime() {
        when(canvases.getCanvasDocument(12, user)).thenThrow(new CustomException(com.endpoint.frelog.global.exception.ErrorCode.CANVAS_NOT_FOUND));
        assertThrows(CustomException.class, () -> controller.execute(12,
                new CanvasRuntimeController.ExecuteRequest("python", "", null), user));
        verifyNoInteractions(runtime);
    }
    @Test void resetUsesTheSameMembershipAndPasswordChecks() {
        CanvasDocument doc = mock(CanvasDocument.class);
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getSettingsRevision()).thenReturn(4L);
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12, user)).thenReturn(doc);
        var request = new CanvasRuntimeController.ResetRequest("grant");
        assertThrows(CustomException.class, () -> controller.reset(12, request, user));
        verifyNoInteractions(runtime);
        when(tokens.validateCanvasPasswordGrant("grant", 7L, 12, 4L)).thenReturn(true);
        when(runtime.reset(12)).thenReturn(Map.of("reset", true));
        assertEquals(true, controller.reset(12, request, user).getBody().get("reset"));
    }

    @Test void runtimeSettingsRequireCanvasAccessAndCurrentPasswordGrant() {
        CanvasDocument doc = mock(CanvasDocument.class);
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getSettingsRevision()).thenReturn(4L);
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12, user)).thenReturn(doc);
        assertThrows(CustomException.class, () -> controller.settings(12, "grant", user));
        var request = new CanvasRuntimeController.ConfigureRequest("cpp", Map.of("engine", "clang21"), 0L, "grant");
        assertThrows(CustomException.class, () -> controller.configure(12, request, user));
        verifyNoInteractions(runtime);
        when(tokens.validateCanvasPasswordGrant("grant", 7L, 12, 4L)).thenReturn(true);
        when(runtime.settings(12)).thenReturn(Map.of("revision", 0));
        when(runtime.configure(12, "cpp", request.config(), 0L)).thenReturn(Map.of("revision", 1));
        assertEquals(0, controller.settings(12, "grant", user).getBody().get("revision"));
        assertEquals(1, controller.configure(12, request, user).getBody().get("revision"));
    }

    @Test void vmTemplatesUseMembershipAndPasswordChecks() {
        CanvasDocument doc = mock(CanvasDocument.class);
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getSettingsRevision()).thenReturn(4L);
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12, user)).thenReturn(doc);
        var request = new CanvasRuntimeController.ResourcesRequest("large", 2L, "grant");
        assertThrows(CustomException.class, () -> controller.resources(12, request, user));
        verifyNoInteractions(runtime);
        when(tokens.validateCanvasPasswordGrant("grant", 7L, 12, 4L)).thenReturn(true);
        when(runtime.resources(12, "large", 2L)).thenReturn(Map.of("resourceTemplate", "large"));
        assertEquals("large", controller.resources(12, request, user).getBody().get("resourceTemplate"));
    }

    @Test void diskGrowthUsesMembershipAndPasswordChecks() {
        CanvasDocument doc = mock(CanvasDocument.class);
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getSettingsRevision()).thenReturn(4L);
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12, user)).thenReturn(doc);
        var request = new CanvasRuntimeController.DiskRequest(12, 2L, "grant");
        assertThrows(CustomException.class, () -> controller.disk(12, request, user));
        verifyNoInteractions(runtime);
        when(tokens.validateCanvasPasswordGrant("grant", 7L, 12, 4L)).thenReturn(true);
        when(runtime.disk(12, 12, 2L)).thenReturn(Map.of("diskGiB", 12));
        assertEquals(12, controller.disk(12, request, user).getBody().get("diskGiB"));
    }

    @Test void installingJavaLibrariesRequiresCanvasAccessAndCurrentPasswordGrant() {
        CanvasDocument doc=mock(CanvasDocument.class);
        when(doc.getCanvasPasswordHash()).thenReturn("hash");when(doc.getSettingsRevision()).thenReturn(4L);
        when(user.getUserId()).thenReturn(7L);when(canvases.getCanvasDocument(12,user)).thenReturn(doc);
        var request=new CanvasRuntimeController.InstallJavaRequest("grant");
        assertThrows(CustomException.class,()->controller.installJavaDependencies(12,request,user));verifyNoInteractions(runtime);
        when(tokens.validateCanvasPasswordGrant("grant",7L,12,4L)).thenReturn(true);
        when(runtime.installJavaDependencies(12)).thenReturn(Map.of("installed",true));
        assertEquals(true,controller.installJavaDependencies(12,request,user).getBody().get("installed"));
    }

    @Test void readOnlyAndExecutionRightsAreCheckedBeforeRuntimeCalls(){
        var doc=new CanvasDocument("Roles",12,1L,null,"default");doc.setPeople(java.util.List.of(1L,7L));doc.setInnerGroup(Map.of("admin-group",java.util.List.of(1L),"default",java.util.List.of(7L)));
        doc.setGroupPermissions(Map.of("default",Map.of("read",true,"write",false,"execute",false)));
        when(user.getUserId()).thenReturn(7L);when(canvases.getCanvasDocument(12,user)).thenReturn(doc);
        assertThrows(CustomException.class,()->controller.execute(12,new CanvasRuntimeController.ExecuteRequest("python","print(1)",null),user));
        assertThrows(CustomException.class,()->controller.configure(12,new CanvasRuntimeController.ConfigureRequest("python",Map.of(),0L,null),user));verifyNoInteractions(runtime);
        doc.setGroupPermissions(Map.of("default",Map.of("read",true,"write",false,"execute",true)));
        when(runtime.execute(12,"python","print(1)")).thenReturn(Map.of("output","1"));assertEquals("1",controller.execute(12,new CanvasRuntimeController.ExecuteRequest("python","print(1)",null),user).getBody().get("output"));
        assertThrows(CustomException.class,()->controller.reset(12,new CanvasRuntimeController.ResetRequest(null),user));
    }
}
