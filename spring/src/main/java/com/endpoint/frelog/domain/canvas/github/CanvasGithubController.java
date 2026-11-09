package com.endpoint.frelog.domain.canvas.github;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;
import java.util.Map;
@RestController
@RequestMapping("/api/canvases/{canvasId}/github")
public class CanvasGithubController {
 private final CanvasGithubService service;
 public CanvasGithubController(CanvasGithubService service){this.service=service;}
 @GetMapping public Map<String,Object> context(@PathVariable int canvasId,@AuthenticationPrincipal CustomUserDetails user){return service.context(canvasId,user);}
 @PostMapping("/bind") public Map<String,Object> bind(@PathVariable int canvasId,@RequestBody CanvasGithubService.Bind body,@AuthenticationPrincipal CustomUserDetails user){return service.bind(canvasId,body,user);}
 @PostMapping("/branches") public Map<String,Object> checkout(@PathVariable int canvasId,@RequestBody CanvasGithubService.BranchRequest body,@AuthenticationPrincipal CustomUserDetails user){return service.checkoutResponse(canvasId,body,user);}
 @PostMapping("/initialized") public void initialized(@PathVariable int canvasId,@RequestBody CanvasGithubService.BranchRequest body,@AuthenticationPrincipal CustomUserDetails user){service.initialized(canvasId,body,user);}
 @PostMapping("/permissions") public Map<String,Object> permissions(@PathVariable int canvasId,@RequestBody CanvasGithubService.Policy body,@AuthenticationPrincipal CustomUserDetails user){return service.policy(canvasId,body,user);}
 @PostMapping("/authorize") public Map<String,Object> authorize(@PathVariable int canvasId,@RequestBody CanvasGithubService.Action body,@AuthenticationPrincipal CustomUserDetails user){return service.authorize(canvasId,body,user);}
}
