package com.endpoint.frelog.domain.canvas.controller;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import com.endpoint.frelog.domain.canvas.service.*;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.global.security.*;
import com.endpoint.frelog.global.exception.CustomException;
class CanvasPasswordSessionControllerTest {
 @Test void requiresLiveBrokerProofAndCurrentPasswordRevision(){
  var canvases=mock(CanvasService.class);var tokens=mock(JwtTokenProvider.class);var broker=mock(CppServerClient.class);var user=mock(CustomUserDetails.class);var doc=mock(CanvasDocument.class);
  when(user.getUserId()).thenReturn(7L);when(doc.getVisibility()).thenReturn("public");when(doc.getCanvasPasswordHash()).thenReturn("hash");when(doc.getPasswordRevision()).thenReturn(4L);when(canvases.getCanvasDocument(12,user)).thenReturn(doc);
  var controller=new CanvasPasswordSessionController(canvases,tokens,broker);var body=new CanvasPasswordSessionController.Renewal("id","proof","expired");
  assertThrows(CustomException.class,()->controller.renew(12,body,user));verifyNoInteractions(tokens);
  when(broker.isCanvasSessionActive(12,7L,"id","proof")).thenReturn(true);
  assertThrows(CustomException.class,()->controller.renew(12,body,user));verify(tokens,never()).createCanvasPasswordGrant(any(),any(),anyLong());
  when(tokens.validateActiveCanvasPasswordGrant("expired",7L,12,4L)).thenReturn(true);when(tokens.createCanvasPasswordGrant(7L,12,4L)).thenReturn("fresh");
  assertEquals("fresh",controller.renew(12,body,user).getBody().get("canvas_password_token"));
 }
 @Test void revokedMembershipCannotReachBroker(){var canvases=mock(CanvasService.class);var tokens=mock(JwtTokenProvider.class);var broker=mock(CppServerClient.class);var user=mock(CustomUserDetails.class);when(user.getUserId()).thenReturn(7L);when(canvases.getCanvasDocument(12,user)).thenThrow(CustomException.class);var c=new CanvasPasswordSessionController(canvases,tokens,broker);assertThrows(CustomException.class,()->c.renew(12,new CanvasPasswordSessionController.Renewal("id","proof","grant"),user));verifyNoInteractions(broker,tokens);}
}
