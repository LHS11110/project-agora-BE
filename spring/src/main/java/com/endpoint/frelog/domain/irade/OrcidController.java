package com.endpoint.frelog.domain.irade;

import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.exception.*;
import jakarta.servlet.http.*;
import org.springframework.http.*;
import org.springframework.web.bind.annotation.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import java.net.URI;
import java.time.*;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import org.springframework.web.util.UriComponentsBuilder;

@RestController @RequestMapping("/api/irade/auth")
public class OrcidController {
    private static final String FLOW="__Secure-IRADE_ORCID_FLOW",HANDOFF="__Secure-IRADE_ORCID_LOGIN";
    private final OrcidConfiguration config;
    private final OrcidService service;
    private final OrcidClient client;
    private final Clock clock;
    private final com.endpoint.frelog.global.security.JwtTokenProvider tokens;
    private final ConcurrentHashMap<String,Handoff> handoffs=new ConcurrentHashMap<>();
    private record Handoff(OrcidService.Completed value,Instant expires,String ip){}
    public OrcidController(OrcidConfiguration config,OrcidService service,OrcidClient client,Clock clock,com.endpoint.frelog.global.security.JwtTokenProvider tokens){this.config=config;this.service=service;this.client=client;this.clock=clock;this.tokens=tokens;}
    @ModelAttribute public void noCache(HttpServletResponse response){response.setHeader("Cache-Control","no-store");response.setHeader("Pragma","no-cache");response.setHeader("Referrer-Policy","no-referrer");}
    @GetMapping("/orcid/status") public Map<String,Object> status(){return Map.of("available",config.enabled(),"issuer",config.issuer(),"registration","automatic");}
    private void available(){if(!config.enabled())throw new org.springframework.web.server.ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE,"ORCID 클라이언트 설정이 필요합니다.");}
    private void sameOrigin(HttpServletRequest request){if(!config.origin().equals(request.getHeader("Origin")))throw new CustomException(ErrorCode.ACCESS_DENIED);}
    private static void cookie(HttpServletResponse response,String name,String value,int seconds){response.addHeader(HttpHeaders.SET_COOKIE,ResponseCookie.from(name,value).httpOnly(true).secure(true).sameSite("Lax").path("/api/irade/auth").maxAge(seconds).build().toString());}
    private String authorize(OrcidService.Start start){return UriComponentsBuilder.fromUriString(config.issuer()+"/oauth/authorize").queryParam("client_id",config.clientId()).queryParam("response_type","code").queryParam("scope","/authenticate").queryParam("redirect_uri",config.redirectUri()).queryParam("state",start.state()).build().encode().toUriString();}
    @GetMapping("/orcid/start") public ResponseEntity<Void> start(@RequestParam(required=false) Integer workspace,HttpServletResponse response){available();var start=service.begin(null,workspace);cookie(response,FLOW,start.browser(),600);return ResponseEntity.status(302).location(URI.create(authorize(start))).build();}
    public record Link(Integer workspace){}
    @PostMapping("/orcid/link") public Map<String,String> link(@RequestBody Link body,@AuthenticationPrincipal CustomUserDetails current,HttpServletRequest request,HttpServletResponse response){
        available();sameOrigin(request);if(current==null)throw new CustomException(ErrorCode.UNAUTHORIZED);
        String authorization=request.getHeader("Authorization");
        if(authorization==null||!authorization.startsWith("Bearer "))throw new CustomException(ErrorCode.UNAUTHORIZED);
        var issued=tokens.getIssuedAt(authorization.substring(7));
        if(issued==null||issued.toInstant().isBefore(clock.instant().minusSeconds(600)))throw new CustomException(ErrorCode.INVALID_CREDENTIALS,"ORCID를 연결하려면 기존 계정으로 다시 로그인하세요.");
        var start=service.begin(current.getUserId(),body.workspace());cookie(response,FLOW,start.browser(),600);return Map.of("authorizationUrl",authorize(start));
    }
    @GetMapping("/orcid/callback") public ResponseEntity<Void> callback(@RequestParam(required=false) String state,@RequestParam(required=false) String code,@RequestParam(required=false) String error,@CookieValue(name=FLOW,required=false) String browser,HttpServletRequest request,HttpServletResponse response){
        available();cookie(response,FLOW,"",0);cookie(response,HANDOFF,"",0);
        Integer workspace=null;String outcome="failed";
        try{
            var attempt=service.consume(state,browser);workspace=attempt.workspaceId;
            if(error!=null){outcome="cancelled";}else{
                var identity=client.exchange(code);var completed=service.complete(identity,attempt.targetUserId,request);
                handoffs.entrySet().removeIf(entry->!entry.getValue().expires().isAfter(clock.instant()));
                if(handoffs.size()>=1000)throw new IllegalStateException("Too many pending logins");
                String raw=OrcidService.random();handoffs.put(OrcidService.hash(raw),new Handoff(completed,clock.instant().plusSeconds(60),request.getRemoteAddr()));cookie(response,HANDOFF,raw,60);outcome="success";
            }
        }catch(RuntimeException rejected){/* Do not log codes, secrets, provider response bodies or tokens. */}
        String location=config.origin()+"/irade/login?orcid="+outcome+(workspace==null?"":"&workspace="+workspace);
        return ResponseEntity.status(303).location(URI.create(location)).build();
    }
    @PostMapping("/session") public OrcidService.Completed session(@CookieValue(name=HANDOFF,required=false) String raw,HttpServletRequest request,HttpServletResponse response){
        sameOrigin(request);cookie(response,HANDOFF,"",0);
        if(raw==null||!raw.matches("[A-Za-z0-9_-]{43}"))throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        var handoff=handoffs.remove(OrcidService.hash(raw));
        if(handoff==null||!handoff.expires().isAfter(clock.instant())||!Objects.equals(handoff.ip(),request.getRemoteAddr()))throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        return handoff.value();
    }
    @GetMapping("/me") public Map<String,Object> me(@AuthenticationPrincipal CustomUserDetails current){var user=service.require(current);return Map.of("userId",user.getUserId(),"name",user.getNickname(),"orcid",user.getOrcid(),"issuer",user.getOrcidIssuer(),"profileUrl",user.getOrcidIssuer()+"/"+user.getOrcid());}
}
