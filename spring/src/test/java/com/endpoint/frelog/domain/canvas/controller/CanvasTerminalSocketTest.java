package com.endpoint.frelog.domain.canvas.controller;
import com.endpoint.frelog.domain.canvas.terminal.CanvasTerminalSocket;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import com.endpoint.frelog.domain.auth.mfa.MfaService;
import com.endpoint.frelog.domain.canvas.bot.CanvasBotService;
import com.endpoint.frelog.global.security.*;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.*;
import org.springframework.web.socket.*;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasTerminalSocketTest {
 final CanvasRuntimeController access=mock(CanvasRuntimeController.class);
 final CanvasRuntimeService runtime=mock(CanvasRuntimeService.class);
 final JwtTokenProvider tokens=mock(JwtTokenProvider.class);
 final CustomUserDetailsService users=mock(CustomUserDetailsService.class);
 final CustomUserDetails user=mock(CustomUserDetails.class);
 final MfaService mfa=mock(MfaService.class);
 final CanvasTerminalSocket handler=new CanvasTerminalSocket(access,runtime,tokens,users,mfa,mock(CanvasBotService.class));
 final ObjectMapper json=new ObjectMapper();
 @BeforeEach void auth(){when(user.getUserId()).thenReturn(7L);when(user.isEnabled()).thenReturn(true);when(user.isAccountNonLocked()).thenReturn(true);when(tokens.validateToken("token")).thenReturn(true);when(tokens.getUserIdFromToken("token")).thenReturn(7L);when(tokens.getClientIpFromToken("token")).thenReturn("127.0.0.1");when(tokens.getEmailFromToken("token")).thenReturn("test@example.invalid");when(users.loadUserByUsername("test@example.invalid")).thenReturn(user);when(mfa.accepts(7L,"token")).thenReturn(true);}
 @AfterEach void clean(){handler.destroy();}
 WebSocketSession socket(String id,String ip)throws Exception{var s=mock(WebSocketSession.class);when(s.getId()).thenReturn(id);when(s.getAttributes()).thenReturn(Map.of("clientIp",ip));when(s.isOpen()).thenReturn(true);handler.afterConnectionEstablished(s);return s;}
 String ticket(){return (String)handler.issue(42,"object","grant","Bearer token","127.0.0.1",user).get("ticket");}
 void attach(WebSocketSession s,String ticket)throws Exception{handler.handleMessage(s,new TextMessage(json.writeValueAsString(Map.of("type","attach","ticket",ticket,"cols",80,"rows",24,"offset",0))));}
 @Test void ticketIsSingleUseAndInputRemainsBoundToItsObject()throws Exception{
  var bridge=mock(CanvasRuntimeService.TerminalBridge.class);when(runtime.attachTerminal(eq(42),eq(7L),eq("object"),eq(80),eq(24),eq(0L),any(),any())).thenReturn(bridge);
  String ticket=ticket();assertTrue(ticket.length()>=43);var first=socket("first","127.0.0.1");attach(first,ticket);
  handler.handleMessage(first,new TextMessage("{\"operation\":\"input\",\"data\":\"pwd\\n\",\"cols\":80,\"rows\":24}"));verify(bridge).send(Map.of("operation","input","data","pwd\n","cols",80,"rows",24));
  var second=socket("second","127.0.0.1");attach(second,ticket);verify(second).close(CloseStatus.POLICY_VIOLATION);verify(runtime,times(1)).attachTerminal(anyInt(),anyLong(),anyString(),anyInt(),anyInt(),anyLong(),any(),any());
  handler.afterConnectionClosed(first,CloseStatus.NORMAL);verify(bridge).close();
 }
 @Test void wrongIpOrMissingTicketCannotStartVm()throws Exception{
  var wrong=socket("wrong","127.0.0.2");attach(wrong,ticket());verify(wrong).close(CloseStatus.POLICY_VIOLATION);
  var anonymous=socket("anonymous","127.0.0.1");attach(anonymous,"invalid");verify(anonymous).close(CloseStatus.POLICY_VIOLATION);verifyNoInteractions(runtime);
 }
 @Test void revokedTokenCannotAttach()throws Exception{
  String ticket=ticket();when(tokens.validateToken("token")).thenReturn(false);var socket=socket("revoked","127.0.0.1");attach(socket,ticket);verify(socket).close(CloseStatus.POLICY_VIOLATION);verifyNoInteractions(runtime);
 }
 @Test void permissionRevocationClosesAnExistingSocket()throws Exception{
  var bridge=mock(CanvasRuntimeService.TerminalBridge.class);when(runtime.attachTerminal(anyInt(),anyLong(),anyString(),anyInt(),anyInt(),anyLong(),any(),any())).thenReturn(bridge);
  String ticket=ticket();var socket=socket("live","127.0.0.1");attach(socket,ticket);
  doThrow(new SecurityException("permission revoked")).when(access).authorizeTerminal(42,"object","grant",user);
  verify(socket,timeout(4000)).close(CloseStatus.POLICY_VIOLATION);verify(bridge).close();
 }
}
