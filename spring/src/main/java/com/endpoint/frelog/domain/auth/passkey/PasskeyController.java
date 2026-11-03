package com.endpoint.frelog.domain.auth.passkey;

import com.endpoint.frelog.global.security.CustomUserDetails;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.http.*;
import jakarta.validation.Valid;
import jakarta.validation.constraints.*;
import org.springframework.http.ResponseCookie;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;
import java.util.Map;

@RestController @RequestMapping("/api/auth/passkeys")
public class PasskeyController {
    private final PasskeyService service;
    private static final String COOKIE="FRELOG_PASSKEY";
    private final ObjectMapper json=new ObjectMapper();
    public PasskeyController(PasskeyService service){this.service=service;}
    public record Finish(@NotBlank @Size(max=128) String challengeToken,@NotNull Map<String,Object> credential,@Size(max=80) String name){}
    @ModelAttribute public void noCache(HttpServletResponse response){response.setHeader("Cache-Control","no-store");response.setHeader("Pragma","no-cache");}
    private void cookie(HttpServletResponse response,String value,int seconds){response.addHeader("Set-Cookie",ResponseCookie.from(COOKIE,value).path("/api/auth/passkeys").httpOnly(true).secure(true).sameSite("Strict").maxAge(seconds).build().toString());}
    private String token(HttpServletRequest request){return request.getHeader("Authorization").substring(7);}
    private Object options(PasskeyService.Options options,HttpServletResponse response) throws Exception {
        cookie(response,options.challengeToken(),300);
        return Map.of("challengeToken",options.challengeToken(),"options",json.readValue(options.optionsJson(),Map.class));
    }
    private String credential(Finish body) throws Exception {String value=json.writeValueAsString(body.credential());if(value.length()>20000)throw new PasskeyRejected("패스키 응답이 너무 큽니다.");return value;}
    @GetMapping("/status") public Object status(){return Map.of("available",true);}
    @GetMapping public Object list(@AuthenticationPrincipal CustomUserDetails user){return service.list(user.getUserId());}
    @PostMapping("/registration/options") public Object registerOptions(@AuthenticationPrincipal CustomUserDetails user,HttpServletRequest request,HttpServletResponse response) throws Exception {
        service.checkOrigin(request);return options(service.registerOptions(user.getUserId(),token(request),request),response);
    }
    @PostMapping("/registration/verify") public void register(@AuthenticationPrincipal CustomUserDetails user,@Valid @RequestBody Finish body,@CookieValue(name=COOKIE,required=false) String cookie,HttpServletRequest request,HttpServletResponse response) throws Exception {
        service.checkOrigin(request);cookie(response,"",0);service.register(user.getUserId(),token(request),body.challengeToken(),cookie,credential(body),body.name(),request);
    }
    @PostMapping("/login/options") public Object loginOptions(HttpServletRequest request,HttpServletResponse response) throws Exception {
        service.checkOrigin(request);return options(service.loginOptions(request),response);
    }
    @PostMapping("/login/verify") public Object login(@Valid @RequestBody Finish body,@CookieValue(name=COOKIE,required=false) String cookie,HttpServletRequest request,HttpServletResponse response) throws Exception {
        service.checkOrigin(request);cookie(response,"",0);return service.login(body.challengeToken(),cookie,credential(body),request);
    }
    @DeleteMapping("/{id}") public void delete(@AuthenticationPrincipal CustomUserDetails user,@PathVariable String id,HttpServletRequest request){service.checkOrigin(request);service.delete(user.getUserId(),id,token(request));}
}
