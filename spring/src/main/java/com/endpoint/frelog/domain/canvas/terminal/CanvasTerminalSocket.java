package com.endpoint.frelog.domain.canvas.terminal;

import com.endpoint.frelog.domain.canvas.controller.CanvasRuntimeController;
import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.service.CanvasRuntimeService;
import com.endpoint.frelog.domain.canvas.bot.CanvasBotService;
import com.endpoint.frelog.domain.auth.mfa.MfaService;
import com.endpoint.frelog.global.security.*;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.*;
import org.springframework.web.socket.handler.TextWebSocketHandler;
import org.springframework.web.socket.handler.ConcurrentWebSocketSessionDecorator;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

@Component
public class CanvasTerminalSocket extends TextWebSocketHandler {
    private final CanvasRuntimeController access;
    private final CanvasRuntimeService runtime;
    private final JwtTokenProvider tokens;
    private final CustomUserDetailsService users;
    private final MfaService mfa;
    private final CanvasBotService bots;
    private final com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions;
    private final ObjectMapper json=new ObjectMapper();
    private final ConcurrentHashMap<String,Ticket> tickets=new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String,Connection> connections=new ConcurrentHashMap<>();
    private final ScheduledExecutorService timer=Executors.newScheduledThreadPool(2,Thread.ofPlatform().daemon(true).name("terminal-watchdog-",0).factory());
    private final java.security.SecureRandom random=new java.security.SecureRandom();
    static final class Ticket {
        final DocumentScope scope;final int canvas;final String object,grant,token,ip;final CustomUserDetails user;final long expires;
        // Captured from the access token while it is verified; a terminal outlives the short token lifetime.
        String sessionId,email,mfaVersion,tokenIp;Long tokenUserId;
        Ticket(DocumentScope scope,int canvas,String object,String grant,String token,String ip,CustomUserDetails user){this.scope=scope;this.canvas=canvas;this.object=object;this.grant=grant;this.token=token;this.ip=ip;this.user=user;this.expires=System.nanoTime()+15_000_000_000L;}
    }
    static final class Connection {
        final WebSocketSession socket;final AtomicBoolean closed=new AtomicBoolean(),checking=new AtomicBoolean();
        volatile Ticket ticket;volatile CanvasRuntimeService.TerminalBridge bridge;volatile ScheduledFuture<?> monitor;
        Connection(WebSocketSession socket){this.socket=new ConcurrentWebSocketSessionDecorator(socket,5000,131072);}
    }
    public CanvasTerminalSocket(CanvasRuntimeController access,CanvasRuntimeService runtime,JwtTokenProvider tokens,CustomUserDetailsService users,MfaService mfa,CanvasBotService bots,com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions){this.authSessions=authSessions;this.access=access;this.runtime=runtime;this.tokens=tokens;this.users=users;this.mfa=mfa;this.bots=bots;}
    public Map<String,Object> issue(int canvas,String object,String grant,String authorization,String ip,CustomUserDetails user){
        return issue(DocumentScope.CANVAS,canvas,object,grant,authorization,ip,user);
    }
    public Map<String,Object> issue(DocumentScope scope,int canvas,String object,String grant,String authorization,String ip,CustomUserDetails user){
        if(scope==null||object==null||!object.matches("[a-zA-Z0-9_-]{1,128}")||authorization==null||!authorization.startsWith("Bearer "))throw new IllegalArgumentException("Invalid terminal ticket request");
        if(scope==DocumentScope.CANVAS)access.authorizeTerminal(canvas,object,grant,user);else access.authorizeTerminal(scope,canvas,object,grant,user);
        tickets.entrySet().removeIf(e->e.getValue().expires<System.nanoTime());
        if(tickets.size()>=1024)throw new IllegalStateException("Terminal ticket capacity reached");
        byte[] bytes=new byte[32];random.nextBytes(bytes);String secret=Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);
        var ticket=new Ticket(scope,canvas,object,grant,authorization.substring(7),ip,user);
        if(!ticket.token.startsWith(CanvasBotService.PREFIX)){
            if(!tokens.validateToken(ticket.token)||!tokens.isUserAccessToken(ticket.token))throw new SecurityException("Authentication expired");
            ticket.sessionId=tokens.getSessionId(ticket.token);ticket.email=tokens.getEmailFromToken(ticket.token);ticket.mfaVersion=tokens.getMfaVersion(ticket.token);
            ticket.tokenIp=tokens.getClientIpFromToken(ticket.token);ticket.tokenUserId=tokens.getUserIdFromToken(ticket.token);
        }
        tickets.put(secret,ticket);
        return Map.of("ticket",secret,"expiresIn",15,"path","/api/terminal/ws");
    }
    @Override public void afterConnectionEstablished(WebSocketSession socket) throws Exception {
        socket.setTextMessageSizeLimit(20000);socket.setBinaryMessageSizeLimit(1);
        if(connections.size()>=64){socket.close(CloseStatus.SERVICE_OVERLOAD);return;}
        var connection=new Connection(socket);connections.put(socket.getId(),connection);
        timer.schedule(()->{if(connection.ticket==null)close(connection,CloseStatus.POLICY_VIOLATION);},5,TimeUnit.SECONDS);
    }
    @Override public void handleTextMessage(WebSocketSession socket,TextMessage message) throws Exception {
        var c=connections.get(socket.getId());if(c==null||c.closed.get())return;
        try {
            var node=json.readTree(message.getPayload());
            if(c.ticket==null){
                String secret=node.path("ticket").asText("");Ticket ticket=tickets.remove(secret);
                if(!"attach".equals(node.path("type").asText())||ticket==null||ticket.expires<System.nanoTime()||!Objects.equals(ticket.ip,socket.getAttributes().get("clientIp")))throw new SecurityException("Invalid ticket");
                c.ticket=ticket;validate(c);
                int cols=node.path("cols").asInt(80),rows=node.path("rows").asInt(24);long offset=node.path("offset").asLong(0);
                if(cols<2||cols>300||rows<2||rows>150||offset<0)throw new IllegalArgumentException("Invalid size");
                var bridge=runtime.attachTerminal(ticket.scope.runtimeTargetId(ticket.canvas),ticket.user.getUserId(),ticket.object,cols,rows,offset,
                    frame->{try{send(c,frame);if(Boolean.TRUE.equals(frame.get("closed")))close(c,CloseStatus.NORMAL);}catch(Exception error){close(c,CloseStatus.SERVER_ERROR);}},
                    ()->close(c,CloseStatus.SERVER_ERROR));
                c.bridge=bridge;if(c.closed.get()){bridge.close();return;}
                c.monitor=timer.scheduleAtFixedRate(()->{
                    if(c.closed.get()||!c.checking.compareAndSet(false,true))return;
                    Thread.ofVirtual().start(()->{try{validate(c);}catch(Exception denied){reject(c);}finally{c.checking.set(false);}});
                },2,2,TimeUnit.SECONDS);
                if(c.closed.get())c.monitor.cancel(false);
                return;
            }
            if(c.bridge==null)throw new IllegalStateException("Terminal is connecting");
            String operation=node.path("operation").asText();String data=node.path("data").asText("");
            int cols=node.path("cols").asInt(80),rows=node.path("rows").asInt(24);
            if(!Set.of("input","resize","close").contains(operation)||data.getBytes(java.nio.charset.StandardCharsets.UTF_8).length>8192||cols<2||cols>300||rows<2||rows>150)throw new IllegalArgumentException("Invalid terminal command");
            c.bridge.send(Map.of("operation",operation,"data",data,"cols",cols,"rows",rows));
        }catch(SecurityException denied){reject(c);}catch(Exception error){close(c,CloseStatus.SERVER_ERROR);}
    }
    private void validate(Connection c){
        var t=c.ticket;CustomUserDetails current;
        if(t.token.startsWith(CanvasBotService.PREFIX)){
            if(t.scope!=DocumentScope.CANVAS)throw new SecurityException("Bot tokens are canvas-only");
            var identity=bots.authenticate(t.token);
            if(identity==null||!bots.allowed(identity,"POST","/api/canvases/"+t.canvas+"/runtime/terminal"))throw new SecurityException("Bot revoked");
            current=identity.user();
        }else{
            if(t.sessionId==null||!authSessions.isActive(t.sessionId,t.user.getUserId())||!Objects.equals(t.user.getUserId(),t.tokenUserId)||!Objects.equals(t.ip,t.tokenIp))throw new SecurityException("Authentication expired");
            current=(CustomUserDetails)users.loadUserByUsername(t.email);
            if(!current.isEnabled()||!current.isAccountNonLocked()||!Objects.equals(current.getUserId(),t.user.getUserId())||!mfa.acceptsVersion(current.getUserId(),t.mfaVersion))throw new SecurityException("Account revoked");
        }
        if(t.scope==DocumentScope.CANVAS)access.authorizeTerminal(t.canvas,t.object,t.grant,current);else access.authorizeTerminal(t.scope,t.canvas,t.object,t.grant,current);
    }
    private void send(Connection c,Map<String,Object> frame)throws Exception{if(!c.closed.get()&&c.socket.isOpen())c.socket.sendMessage(new TextMessage(json.writeValueAsString(frame)));}
    private void reject(Connection c){try{send(c,Map.of("type","error","retryable",false,"message","터미널 인증 또는 실행 권한이 만료되었습니다."));}catch(Exception ignored){}close(c,CloseStatus.POLICY_VIOLATION);}
    private void close(Connection c,CloseStatus status){if(!c.closed.compareAndSet(false,true))return;connections.remove(c.socket.getId());if(c.monitor!=null)c.monitor.cancel(false);if(c.bridge!=null)c.bridge.close();try{if(c.socket.isOpen())c.socket.close(status);}catch(Exception ignored){}}
    @Override public void afterConnectionClosed(WebSocketSession socket,CloseStatus status){var c=connections.get(socket.getId());if(c!=null)close(c,status);}
    @Override public void handleTransportError(WebSocketSession socket,Throwable error){var c=connections.get(socket.getId());if(c!=null)close(c,CloseStatus.SERVER_ERROR);}
    @jakarta.annotation.PreDestroy public void destroy(){connections.values().forEach(c->close(c,CloseStatus.GOING_AWAY));timer.shutdownNow();tickets.clear();}
}
