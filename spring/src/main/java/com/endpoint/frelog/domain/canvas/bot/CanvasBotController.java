package com.endpoint.frelog.domain.canvas.bot;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;
import jakarta.servlet.http.HttpServletResponse;
import java.util.List;
@RestController @RequestMapping("/api/canvases/{canvasId}/bots")
public class CanvasBotController {
    private final CanvasBotService bots;
    public CanvasBotController(CanvasBotService bots){this.bots=bots;}
    public record Create(String name){}
    @ModelAttribute public void noCache(HttpServletResponse response){response.setHeader("Cache-Control","no-store");}
    @PostMapping public CanvasBotService.Created create(@PathVariable int canvasId,@RequestBody Create body,@AuthenticationPrincipal CustomUserDetails user){return bots.create(canvasId,body.name(),user);}
    @GetMapping public List<CanvasBotService.Bot> list(@PathVariable int canvasId,@AuthenticationPrincipal CustomUserDetails user){return bots.list(canvasId,user);}
    @DeleteMapping("/{botId}") public void revoke(@PathVariable int canvasId,@PathVariable long botId,@AuthenticationPrincipal CustomUserDetails user){bots.revoke(canvasId,botId,user);}
    @PostMapping("/{botId}/rotate") public CanvasBotService.Created rotate(@PathVariable int canvasId,@PathVariable long botId,@AuthenticationPrincipal CustomUserDetails user){return bots.rotate(canvasId,botId,user);}
}
