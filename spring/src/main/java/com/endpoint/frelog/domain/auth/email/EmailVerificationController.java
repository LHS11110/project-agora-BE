package com.endpoint.frelog.domain.auth.email;
import jakarta.validation.Valid;
import jakarta.validation.constraints.*;
import jakarta.servlet.http.*;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import java.util.Map;
@RestController @RequestMapping("/api/auth/email")
public class EmailVerificationController {
 private final EmailVerificationService service;
 public EmailVerificationController(EmailVerificationService service){this.service=service;}
 public record Address(@NotBlank @Email @Size(max=255) String email){}
 public record Code(@NotBlank @Email @Size(max=255) String email,@NotBlank @Size(max=128) String challengeId,@NotBlank @Pattern(regexp="[0-9]{6}") String code){}
 @ModelAttribute public void noCache(HttpServletResponse r){r.setHeader("Cache-Control","no-store");r.setHeader("Pragma","no-cache");}
 @GetMapping("/status") public Object status(){return Map.of("available",service.available());}
 @PostMapping("/request") public Object request(@Valid @RequestBody Address a,HttpServletRequest r){return ResponseEntity.accepted().body(service.request(a.email(),r.getRemoteAddr()));}
 @PostMapping("/verify") public Object verify(@Valid @RequestBody Code c){return service.verify(c.email(),c.challengeId(),c.code());}
}
