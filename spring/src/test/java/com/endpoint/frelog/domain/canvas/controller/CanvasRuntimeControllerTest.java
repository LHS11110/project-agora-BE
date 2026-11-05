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
        when(doc.getVisibility()).thenReturn("public");
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getPasswordRevision()).thenReturn(4L);
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

    @Test void buildRequiresTheSameAccessAndCurrentPasswordGrant() {
        CanvasDocument doc=mock(CanvasDocument.class);
        when(doc.getVisibility()).thenReturn("public");
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getPasswordRevision()).thenReturn(4L);
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12,user)).thenReturn(doc);
        var request=new CanvasRuntimeController.ExecuteRequest("cpp","int main(){}","grant");
        assertThrows(CustomException.class,()->controller.build(12,request,user));
        verifyNoInteractions(runtime);
        when(tokens.validateCanvasPasswordGrant("grant",7L,12,4L)).thenReturn(true);
        when(runtime.build(12,"cpp","int main(){}")).thenReturn(Map.of("exitCode",0));
        assertEquals(0,controller.build(12,request,user).getBody().get("exitCode"));
        verify(runtime).build(12,"cpp","int main(){}");
    }

    @Test void rejectedMembershipNeverReachesRuntime() {
        when(canvases.getCanvasDocument(12, user)).thenThrow(new CustomException(com.endpoint.frelog.global.exception.ErrorCode.CANVAS_NOT_FOUND));
        assertThrows(CustomException.class, () -> controller.execute(12,
                new CanvasRuntimeController.ExecuteRequest("python", "", null), user));
        verifyNoInteractions(runtime);
    }
    @Test void resetUsesTheSameMembershipAndPasswordChecks() {
        CanvasDocument doc = mock(CanvasDocument.class);
        when(doc.getVisibility()).thenReturn("public");
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getPasswordRevision()).thenReturn(4L);
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
        when(doc.getVisibility()).thenReturn("public");
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getPasswordRevision()).thenReturn(4L);
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
        when(doc.getVisibility()).thenReturn("public");
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getPasswordRevision()).thenReturn(4L);
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
        when(doc.getVisibility()).thenReturn("public");
        when(doc.getCanvasPasswordHash()).thenReturn("hash");
        when(doc.getPasswordRevision()).thenReturn(4L);
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
        when(doc.getVisibility()).thenReturn("public");
        when(doc.getCanvasPasswordHash()).thenReturn("hash");when(doc.getPasswordRevision()).thenReturn(4L);
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
    @Test void privateCanvasLegacyPasswordDoesNotBlockInvitedUserRuntime() {
        var doc = new CanvasDocument("Private", 12, 7L, "legacy-hash", "default");
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12, user)).thenReturn(doc);
        when(runtime.execute(12, "python", "print(1)")).thenReturn(Map.of("output", "1"));
        assertEquals("1", controller.execute(12, new CanvasRuntimeController.ExecuteRequest("python", "print(1)", null), user).getBody().get("output"));
        verifyNoInteractions(tokens);
    }

    @Test void passwordGrantSurvivesUnrelatedSettingsButNotPasswordChanges() {
        var doc = new CanvasDocument("Public", 12, 7L, "hash", "default");
        doc.setVisibility("public"); doc.setPasswordRevision(4L); doc.setSettingsRevision(99L);
        when(user.getUserId()).thenReturn(7L);
        when(canvases.getCanvasDocument(12, user)).thenReturn(doc);
        when(tokens.validateCanvasPasswordGrant("grant", 7L, 12, 4L)).thenReturn(true);
        when(runtime.execute(12, "python", "print(1)")).thenReturn(Map.of("output", "1"));
        var request = new CanvasRuntimeController.ExecuteRequest("python", "print(1)", "grant");
        assertEquals("1", controller.execute(12, request, user).getBody().get("output"));
        doc.setSettingsRevision(100L);
        assertEquals("1", controller.execute(12, request, user).getBody().get("output"));
        doc.setPasswordRevision(5L);
        assertThrows(CustomException.class, () -> controller.execute(12, request, user));
    }

    @Test void outputFramesAreFlushedInOrderAndPermissionCheckedBeforeStreaming() throws Exception {
        var doc = new CanvasDocument("Private",12,7L,null,"default");
        when(user.getUserId()).thenReturn(7L);when(canvases.getCanvasDocument(12,user)).thenReturn(doc);
        when(runtime.executeStream(eq(12),eq("python"),eq("print(1)"),isNull(),any())).thenAnswer(invocation -> {
            java.util.function.Consumer<Map<String,Object>> consumer = invocation.getArgument(4);
            consumer.accept(Map.of("type","output","output","first\n"));
            consumer.accept(Map.of("type","output","output","last\n"));
            return Map.of("exitCode",0,"output","first\nlast\n");
        });
        var response = controller.executeStream(12,new CanvasRuntimeController.ExecuteRequest("python","print(1)",null),user,new org.springframework.mock.web.MockHttpServletResponse());
        assertEquals("no",response.getHeaders().getFirst("X-Accel-Buffering"));
        var bytes = new java.io.ByteArrayOutputStream();response.getBody().writeTo(bytes);
        var lines = bytes.toString(java.nio.charset.StandardCharsets.UTF_8).lines().toList();
        assertEquals(3,lines.size());assertTrue(lines.get(0).contains("first"));assertTrue(lines.get(1).contains("last"));assertTrue(lines.get(2).contains("result"));
        doc.setAdminUserId(1L);doc.setInnerGroup(Map.of("default",java.util.List.of(7L)));
        doc.setGroupPermissions(Map.of("default",Map.of("read",true,"write",false,"execute",false)));
        assertThrows(CustomException.class,()->controller.executeStream(12,new CanvasRuntimeController.ExecuteRequest("python","print(1)",null),user,new org.springframework.mock.web.MockHttpServletResponse()));
    }


    @Test void terminalBindsOwnerAndRequiresObjectGroupExecution() {
        var doc=new CanvasDocument();doc.setCanvasId(12);doc.setAdminUserId(99L);
        doc.setInnerGroup(Map.of("readers",java.util.List.of(7L),"other",java.util.List.of(8L)));
        doc.setGroupPermissions(Map.of("readers",Map.of("read",true,"execute",true,"write",false),"other",Map.of("read",true,"execute",true,"write",true)));
        doc.setItems(Map.of("terminal-1",Map.of("kind","shape","runtimeRole","terminal","permission",java.util.List.of("readers"))));
        when(user.getUserId()).thenReturn(7L);when(canvases.getCanvasItemDocument(eq(12),eq(user),anyString())).thenReturn(doc);
        var request=new CanvasRuntimeController.TerminalRequest("terminal-1","open","",80,24,0L,null);
        when(runtime.terminal(12,7L,"terminal-1","open","",80,24,0L)).thenReturn(Map.of("offset",0));
        assertEquals(0,controller.terminal(12,request,user).getBody().get("offset"));
        verify(runtime).terminal(12,7L,"terminal-1","open","",80,24,0L);
        clearInvocations(runtime);
        doc.setItems(Map.of("terminal-1",Map.of("kind","shape","runtimeRole","terminal","permission",java.util.List.of("other"))));
        assertThrows(CustomException.class,()->controller.terminal(12,request,user));verifyNoInteractions(runtime);
        doc.setItems(Map.of());assertThrows(CustomException.class,()->controller.terminal(12,request,user));
    }
    @Test void terminalRejectsRevokedExecutionAndPasswordGrant() {
        var doc=new CanvasDocument();doc.setAdminUserId(99L);doc.setVisibility("public");doc.setCanvasPasswordHash("hash");
        doc.setInnerGroup(Map.of("default",java.util.List.of(7L)));doc.setInitGroup("default");
        doc.setGroupPermissions(Map.of("default",Map.of("read",true,"write",true,"execute",false)));
        when(user.getUserId()).thenReturn(7L);when(canvases.getCanvasItemDocument(eq(12),eq(user),anyString())).thenReturn(doc);
        var request=new CanvasRuntimeController.TerminalRequest("term","input","pwd\n",80,24,0L,null);
        assertThrows(CustomException.class,()->controller.terminal(12,request,user));
        doc.setGroupPermissions(Map.of("default",Map.of("read",true,"write",true,"execute",true)));
        assertThrows(CustomException.class,()->controller.terminal(12,request,user));verifyNoInteractions(runtime);
    }
    @Test void notebookRequiresObjectExecutionAndRechecksRevocationBeforeReturningOutput() {
        var doc=new CanvasDocument();doc.setCanvasId(12);doc.setAdminUserId(99L);
        doc.setInnerGroup(Map.of("readers",java.util.List.of(7L)));
        doc.setGroupPermissions(Map.of("readers",Map.of("read",true,"execute",true,"write",false)));
        doc.setItems(Map.of("nb",Map.of("kind","shape","runtimeRole","notebook","permission",java.util.List.of("readers"))));
        when(user.getUserId()).thenReturn(7L);when(canvases.getCanvasItemDocument(12,user,"nb")).thenReturn(doc);
        var request=new CanvasRuntimeController.NotebookRequest("nb","execute","1+1",null);
        when(runtime.notebook(12,"nb","execute","1+1")).thenReturn(Map.of("exitCode",0));
        assertEquals(0,controller.notebook(12,request,user).getBody().get("exitCode"));
        when(runtime.notebook(12,"nb","execute","1+1")).thenAnswer(invocation->{doc.setGroupPermissions(Map.of("readers",Map.of("read",true,"execute",false,"write",false)));return Map.of("exitCode",0);});
        assertThrows(CustomException.class,()->controller.notebook(12,request,user));
        clearInvocations(runtime);assertThrows(CustomException.class,()->controller.notebook(12,request,user));verifyNoInteractions(runtime);
        doc.setGroupPermissions(Map.of("readers",Map.of("read",true,"execute",true,"write",false)));
        when(canvases.getCanvasDocument(12,user)).thenReturn(doc);
        assertThrows(CustomException.class,()->controller.notebook(12,new CanvasRuntimeController.NotebookRequest("nb","prepare","",null),user));verifyNoInteractions(runtime);
    }

}
