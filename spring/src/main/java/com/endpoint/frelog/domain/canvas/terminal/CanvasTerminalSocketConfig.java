package com.endpoint.frelog.domain.canvas.terminal;
import org.springframework.context.annotation.Configuration;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.web.socket.config.annotation.*;
import org.springframework.web.socket.server.HandshakeInterceptor;
import org.springframework.http.server.*;
import java.util.*;
@Configuration
@EnableWebSocket
public class CanvasTerminalSocketConfig implements WebSocketConfigurer {
 private final CanvasTerminalSocket handler;private final String[] origins;
 public CanvasTerminalSocketConfig(CanvasTerminalSocket handler,@Value("${app.security.cors.allowed-origins:}")String origins){this.handler=handler;this.origins=Arrays.stream(origins.split(",")).map(String::trim).filter(s->!s.isEmpty()).toArray(String[]::new);}
 public void registerWebSocketHandlers(WebSocketHandlerRegistry registry){
  registry.addHandler(handler,"/api/terminal/ws").setAllowedOrigins(origins).addInterceptors(new HandshakeInterceptor(){
   public boolean beforeHandshake(ServerHttpRequest request,ServerHttpResponse response,org.springframework.web.socket.WebSocketHandler h,Map<String,Object> attributes){if(!(request instanceof ServletServerHttpRequest servlet))return false;attributes.put("clientIp",servlet.getServletRequest().getRemoteAddr());return true;}
   public void afterHandshake(ServerHttpRequest request,ServerHttpResponse response,org.springframework.web.socket.WebSocketHandler h,Exception error){}
  });
 }
}
