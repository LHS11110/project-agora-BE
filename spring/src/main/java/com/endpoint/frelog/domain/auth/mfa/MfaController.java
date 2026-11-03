package com.endpoint.frelog.domain.auth.mfa;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import jakarta.validation.Valid;
import jakarta.validation.constraints.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;
import java.util.Map;
@RestController @RequestMapping("/api/auth/mfa")
public class MfaController {
    private final MfaService service;
    public MfaController(MfaService service){this.service=service;}
    public record Code(@NotBlank @Size(max=64) String code){}
    public record Verify(@NotBlank @Size(max=128) String challengeToken,@NotBlank @Size(max=64) String code){}
    private String token(HttpServletRequest r){String v=r.getHeader("Authorization");return v==null?"":v.substring(7);}
    @ModelAttribute public void noCache(HttpServletResponse response){response.setHeader("Cache-Control","no-store");response.setHeader("Pragma","no-cache");}
    @GetMapping public Object status(@AuthenticationPrincipal CustomUserDetails user){return service.status(user.getUserId());}
    @PostMapping("/enrollment") public Object begin(@AuthenticationPrincipal CustomUserDetails user,HttpServletRequest r){return service.begin(user.getUserId(),token(r));}
    @PostMapping("/enrollment/confirm") public Object confirm(@AuthenticationPrincipal CustomUserDetails user,@Valid @RequestBody Code code,HttpServletRequest r){return service.confirm(user.getUserId(),code.code(),token(r),r);}
    @DeleteMapping("/enrollment") public void cancel(@AuthenticationPrincipal CustomUserDetails user){service.cancel(user.getUserId());}
    @PostMapping("/disable") public void disable(@AuthenticationPrincipal CustomUserDetails user,@Valid @RequestBody Code code){service.disable(user.getUserId(),code.code());}
    @PostMapping("/recovery-codes") public Object regenerate(@AuthenticationPrincipal CustomUserDetails user,@Valid @RequestBody Code code){return Map.of("recoveryCodes",service.regenerate(user.getUserId(),code.code()));}
    @PostMapping("/verify") public Object verify(@Valid @RequestBody Verify v,HttpServletRequest r){return service.complete(v.challengeToken(),v.code(),r);}
}
