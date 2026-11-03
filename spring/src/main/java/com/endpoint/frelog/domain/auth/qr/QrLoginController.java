package com.endpoint.frelog.domain.auth.qr;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.servlet.http.*;
import jakarta.validation.Valid;
import jakarta.validation.constraints.*;
import org.springframework.http.ResponseCookie;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;
@RestController @RequestMapping("/api/auth/qr")
public class QrLoginController {
 private final QrLoginService service;public QrLoginController(QrLoginService service){this.service=service;}
 public record Proof(@NotBlank @Pattern(regexp="[0-9a-f-]{36}") String requestId,@NotBlank @Size(max=128) String secret){}
 public record Decision(@NotBlank @Pattern(regexp="[0-9a-f-]{36}") String requestId,@NotBlank @Size(max=128) String secret,@Size(max=6) String code,boolean approve){}
 @ModelAttribute public void headers(HttpServletResponse r){r.setHeader("Cache-Control","no-store");r.setHeader("Pragma","no-cache");r.setHeader("Referrer-Policy","no-referrer");}
 private String cookieName(String id){return "FRELOG_QR_"+id;}
 private String browser(HttpServletRequest r,String id){if(r.getCookies()!=null)for(var c:r.getCookies())if(c.getName().equals(cookieName(id)))return c.getValue();return null;}
 private void cookie(HttpServletResponse r,String id,String value,int age){r.addHeader("Set-Cookie",ResponseCookie.from(cookieName(id),value).path("/api/auth/qr").httpOnly(true).secure(true).sameSite("Strict").maxAge(age).build().toString());}
 @PostMapping("/start") public Object start(HttpServletRequest r,HttpServletResponse response){service.checkOrigin(r);var start=service.start(r);cookie(response,start.response().requestId(),start.browserToken(),180);return start.response();}
 @PostMapping("/poll") public Object poll(@Valid @RequestBody Proof p,HttpServletRequest r,HttpServletResponse response){service.checkOrigin(r);var result=service.poll(p.requestId(),p.secret(),browser(r,p.requestId()),r);if(java.util.Set.of("COMPLETE","DENIED","USED","CANCELLED").contains(result.status()))cookie(response,p.requestId(),"",0);return result;}
 @PostMapping("/cancel") public void cancel(@Valid @RequestBody Proof p,HttpServletRequest r,HttpServletResponse response){service.checkOrigin(r);service.cancel(p.requestId(),p.secret(),browser(r,p.requestId()),r);cookie(response,p.requestId(),"",0);}
 @PostMapping("/preview") public Object preview(@Valid @RequestBody Proof p,HttpServletRequest r){service.checkOrigin(r);return service.preview(p.requestId(),p.secret());}
 @PostMapping("/decide") public void decide(@Valid @RequestBody Decision p,@AuthenticationPrincipal CustomUserDetails user,HttpServletRequest r){service.checkOrigin(r);service.decide(p.requestId(),p.secret(),p.code(),p.approve(),user.getUserId(),r.getHeader("Authorization").substring(7));}
}
