package com.endpoint.frelog.domain.canvas.controller;

import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.domain.canvas.service.CanvasGroupPermissions;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.web.bind.annotation.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.http.*;
import java.util.*;

@RestController
@RequestMapping("/api/canvases")
public class CanvasPasswordSessionController {
 private final CanvasService canvases; private final JwtTokenProvider tokens; private final CppServerClient broker;
 public CanvasPasswordSessionController(CanvasService canvases,JwtTokenProvider tokens,CppServerClient broker){this.canvases=canvases;this.tokens=tokens;this.broker=broker;}
 public record Renewal(String connectionId,String proof,String passwordGrant){}
 @PostMapping("/{canvasId}/password-session")
 public ResponseEntity<Map<String,Object>> renew(@PathVariable Integer canvasId,@RequestBody Renewal body,@AuthenticationPrincipal CustomUserDetails user){
  if(user==null||user.getUserId()==null)throw new CustomException(ErrorCode.UNAUTHORIZED);
  if(body.connectionId()==null||body.connectionId().length()>256||body.proof()==null||body.proof().length()>256||body.passwordGrant()==null||body.passwordGrant().length()>16384)throw new CustomException(ErrorCode.ACCESS_DENIED);
  var doc=canvases.getCanvasDocument(canvasId,user);
  if(!CanvasGroupPermissions.allows(doc,user,"read"))throw new CustomException(ErrorCode.ACCESS_DENIED);
  if(!broker.isCanvasSessionActive(canvasId,user.getUserId(),body.connectionId(),body.proof()))throw new CustomException(ErrorCode.ACCESS_DENIED);
  Map<String,Object> result=new LinkedHashMap<>();
  if("public".equals(doc.getVisibility())&&doc.getCanvasPasswordHash()!=null&&!doc.getCanvasPasswordHash().isBlank()){
   if(!tokens.validateActiveCanvasPasswordGrant(body.passwordGrant(),user.getUserId(),canvasId,doc.getPasswordRevision()))throw new CustomException(ErrorCode.CANVAS_PASSWORD_REQUIRED);
   result.put("canvas_password_token",tokens.createCanvasPasswordGrant(user.getUserId(),canvasId,doc.getPasswordRevision()));
  }else result.put("canvas_password_token",null);
  return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(result);
 }
}
