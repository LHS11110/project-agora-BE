package com.endpoint.frelog.domain.canvas.socket;
import com.endpoint.frelog.domain.canvas.service.*;
import com.endpoint.frelog.domain.canvas.controller.*;
import com.endpoint.frelog.domain.canvas.bot.*;
import com.endpoint.frelog.domain.canvas.github.*;
import com.endpoint.frelog.domain.canvas.terminal.*;
import com.endpoint.frelog.domain.user.controller.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.web.server.ResponseStatusException;
import java.util.*;
import java.nio.file.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasSocketServiceControllerTest {
 static final String TOKEN="test-internal-token-at-least-32-characters";
 @TempDir Path directory;
 final ObjectMapper json=new ObjectMapper();
 final CanvasService canvases=mock(CanvasService.class);final CanvasResourceService resources=mock(CanvasResourceService.class);final UserRepository users=mock(UserRepository.class);
 CanvasSocketServiceController controller(boolean write){
  User user=mock(User.class);when(user.getUserId()).thenReturn(2L);when(user.isActive()).thenReturn(true);when(users.findById(2L)).thenReturn(Optional.of(user));
  var doc=new CanvasDocument("Test",1,1L,null,"team");doc.setPeople(List.of(1L,2L));doc.setInnerGroup(Map.of("admin-group",List.of(1L),"team",List.of(2L)));doc.setGroupPermissions(Map.of("team",Map.of("read",true,"write",write,"execute",false)));
  when(canvases.getCanvasItemDocument(eq(1),any(),eq("socket-service-access"))).thenReturn(doc);when(resources.getCanvasDirectory(1)).thenReturn(directory);
  return new CanvasSocketServiceController(canvases,resources,mock(CanvasMediaController.class),mock(CanvasPdfController.class),mock(CanvasController.class),mock(CanvasBotService.class),mock(CanvasGithubService.class),mock(UserProfileController.class),mock(CanvasTerminalSocket.class),users,json,mock(com.endpoint.frelog.domain.link.LinkPreviewController.class),TOKEN);
 }
 @Test void mvcRequestConversionAndErrorStatusMatchTheDeployedJacksonVersion()throws Exception{
  var mvc=org.springframework.test.web.servlet.setup.MockMvcBuilders.standaloneSetup(controller(true)).setControllerAdvice(new com.endpoint.frelog.global.exception.GlobalExceptionHandler()).build();
  var body="{\"canvasId\":1,\"userId\":2,\"request\":{\"operation\":\"api\",\"method\":\"GET\",\"path\":\"/bots\"}}";
  mvc.perform(org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post("/internal/canvas/socket").header("X-Agora-Internal-Token",TOKEN).contentType("application/json").content(body)).andExpect(org.springframework.test.web.servlet.result.MockMvcResultMatchers.status().isOk());
  mvc.perform(org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post("/internal/canvas/socket").header("X-Agora-Internal-Token","wrong").contentType("application/json").content(body)).andExpect(org.springframework.test.web.servlet.result.MockMvcResultMatchers.status().isUnauthorized());
 }
 @Test void internalSecretIsRequiredBeforeReadingIdentity()throws Exception{
  var c=controller(true);var error=assertThrows(ResponseStatusException.class,()->c.exchange("wrong",json.readTree("{}")));assertEquals(401,error.getStatusCode().value());verifyNoInteractions(canvases);
 }
 @Test void readersCannotBeginUploads()throws Exception{
  var c=controller(false);var request=json.readTree("{\"canvasId\":1,\"userId\":2,\"request\":{\"operation\":\"upload_begin\",\"path\":\"/media\",\"files\":[{\"field\":\"file\",\"size\":4}]}}");
  assertEquals(403,assertThrows(ResponseStatusException.class,()->c.exchange(TOKEN,request)).getStatusCode().value());verifyNoInteractions(resources);
 }
 @Test void chunkRetriesAreIdempotentAndFutureOffsetsAreRejected()throws Exception{
  var c=controller(true);var request=json.readTree("{\"canvasId\":1,\"userId\":2,\"request\":{\"operation\":\"upload_begin\",\"path\":\"/media\",\"files\":[{\"field\":\"file\",\"name\":\"sample.mp4\",\"type\":\"video/mp4\",\"size\":12}]}}");
  var started=(Map<?,?>)c.exchange(TOKEN,request);var body=(com.fasterxml.jackson.databind.node.ObjectNode)request.path("request");body.remove("files");body.put("operation","upload_chunk");body.put("uploadId",started.get("uploadId").toString());body.put("index",0);body.put("offset",0);body.put("data","AQIDBA==");
  assertEquals(4L,((Map<?,?>)c.exchange(TOKEN,request)).get("offset"));assertEquals(4L,((Map<?,?>)c.exchange(TOKEN,request)).get("offset"));
  body.put("offset",5);assertEquals(409,assertThrows(ResponseStatusException.class,()->c.exchange(TOKEN,request)).getStatusCode().value());
  body.put("operation","upload_cancel");c.exchange(TOKEN,request);assertEquals(410,assertThrows(ResponseStatusException.class,()->c.exchange(TOKEN,request)).getStatusCode().value());
 }
}
