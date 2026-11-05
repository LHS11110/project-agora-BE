package com.endpoint.frelog.domain.canvas.terminal;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.web.bind.annotation.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.http.*;
import java.util.Map;
@RestController
@RequestMapping("/api/canvases")
public class CanvasTerminalTicketController {
 private final CanvasTerminalSocket socket;
 public CanvasTerminalTicketController(CanvasTerminalSocket socket){this.socket=socket;}
 public record TicketRequest(String objectId,String passwordGrant){}
 @PostMapping("/{canvasId}/runtime/terminal/ticket")
 public ResponseEntity<Map<String,Object>> issue(@PathVariable Integer canvasId,@RequestBody TicketRequest body,@RequestHeader("Authorization")String auth,jakarta.servlet.http.HttpServletRequest request,@AuthenticationPrincipal CustomUserDetails user){return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(socket.issue(canvasId,body.objectId(),body.passwordGrant(),auth,request.getRemoteAddr(),user));}
}
