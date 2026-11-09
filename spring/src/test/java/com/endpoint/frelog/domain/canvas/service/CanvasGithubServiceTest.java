package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.domain.canvas.github.CanvasGithubService;
import com.endpoint.frelog.domain.canvas.dto.*;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.exception.CustomException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import java.nio.file.Path;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasGithubServiceTest {
 @TempDir Path directory;
 private CanvasUpdateDtos.SettingsResponse settings(int id,boolean admin){return new CanvasUpdateDtos.SettingsResponse(id,"Canvas","",false,0L,List.of(),"private",Map.of(),admin,Map.of(),"default",null);}
 @Test void branchRoomsAreDistinctDurableAndIdempotent(){
  var canvases=mock(CanvasService.class);var resources=mock(CanvasResourceService.class);var user=mock(CustomUserDetails.class);
  when(resources.getCanvasDirectory(anyInt())).thenAnswer(inv->directory.resolve(inv.getArgument(0).toString()));
  when(canvases.getCanvasSettings(anyInt(),eq(user))).thenAnswer(inv->settings(inv.getArgument(0),true));
  when(canvases.createBranchCanvas(10,"feature",user)).thenReturn(new CanvasSummaryResponse(20,"","","Feature",0,"private"));
  var service=new CanvasGithubService(canvases,resources);
  service.bind(10,new CanvasGithubService.Bind("Owner","Repo","canvas.json","main"),user);
  var child=service.checkout(10,new CanvasGithubService.BranchRequest("feature"),user);assertEquals(20,child.canvasId());assertFalse(child.initialized());
  assertEquals(child,service.checkout(10,new CanvasGithubService.BranchRequest("feature"),user));verify(canvases,times(1)).createBranchCanvas(10,"feature",user);
  service.initialized(20,new CanvasGithubService.BranchRequest("feature"),user);
  var restarted=new CanvasGithubService(canvases,resources);var ctx=restarted.context(20,user);assertEquals(10,ctx.get("rootId"));assertEquals("feature",ctx.get("currentBranch"));
  assertTrue(restarted.checkout(10,new CanvasGithubService.BranchRequest("feature"),user).initialized());
  when(canvases.createBranchCanvas(20,"nested",user)).thenReturn(new CanvasSummaryResponse(30,"","","Nested",0,"public"));
  restarted.checkout(20,new CanvasGithubService.BranchRequest("nested"),user);
  verify(canvases).createBranchCanvas(20,"nested",user);
 }
 @Test void nonAdminsCannotAllocateOrBindRooms(){
  var canvases=mock(CanvasService.class);var resources=mock(CanvasResourceService.class);var user=mock(CustomUserDetails.class);
  when(canvases.getCanvasSettings(10,user)).thenReturn(settings(10,false));
  var service=new CanvasGithubService(canvases,resources);
  assertThrows(CustomException.class,()->service.bind(10,new CanvasGithubService.Bind("owner","repo","c.json","main"),user));
  verify(canvases,never()).createBranchCanvas(anyInt(),anyString(),any());
 }
 @Test void delegatedActionsDefaultDenyAndRecheckGroupPolicy() {
  var canvases=mock(CanvasService.class);var resources=mock(CanvasResourceService.class);
  var adminUser=new com.endpoint.frelog.domain.user.entity.User("admin@test.invalid","x","Admin",1,com.endpoint.frelog.domain.user.entity.Role.ROLE_USER);
  var memberUser=new com.endpoint.frelog.domain.user.entity.User("member@test.invalid","x","Member",2,com.endpoint.frelog.domain.user.entity.Role.ROLE_USER);
  org.springframework.test.util.ReflectionTestUtils.setField(adminUser,"userId",1L);org.springframework.test.util.ReflectionTestUtils.setField(memberUser,"userId",2L);
  var admin=new CustomUserDetails(adminUser);var member=new CustomUserDetails(memberUser);
  when(resources.getCanvasDirectory(anyInt())).thenAnswer(inv->directory.resolve(inv.getArgument(0).toString()));
  var groups=Map.of("default",List.of(new CanvasUpdateDtos.ParticipantResponse("Member",2)));
  var policies=Map.of("default",Map.of("read",true,"write",true,"execute",false));
  when(canvases.getCanvasSettings(anyInt(),eq(admin))).thenReturn(new CanvasUpdateDtos.SettingsResponse(10,"C","",false,0L,List.of(),"private",policies,true,groups,"default",null));
  when(canvases.getCanvasSettings(anyInt(),eq(member))).thenReturn(new CanvasUpdateDtos.SettingsResponse(10,"C","",false,0L,List.of(),"private",policies,false,groups,"default",null));
  var service=new CanvasGithubService(canvases,resources);var target=new CanvasGithubService.Bind("owner","repo","canvas.json","main");service.bind(10,target,admin);
  var push=new CanvasGithubService.Action("push","owner","repo","canvas.json","main");
  assertThrows(CustomException.class,()->service.authorize(10,push,member));
  assertThrows(CustomException.class,()->service.bind(10,target,member));
  service.policy(10,new CanvasGithubService.Policy(Map.of("default",Set.of("push","pr","switch_branch"))),admin);
  assertNotNull(service.authorize(10,push,member));
  assertThrows(CustomException.class,()->service.authorize(10,new CanvasGithubService.Action("commit","owner","repo","canvas.json","main"),member));
  assertThrows(CustomException.class,()->service.authorize(10,new CanvasGithubService.Action("push","attacker","repo","canvas.json","main"),member));
  service.policy(10,new CanvasGithubService.Policy(Map.of()),admin);assertThrows(CustomException.class,()->service.authorize(10,push,member));
  service.bind(10,new CanvasGithubService.Bind("owner","changed","new.json","main"),admin);
  assertEquals("changed",((CanvasGithubService.Project)service.context(10,admin).get("project")).target().repo());
 }
}
