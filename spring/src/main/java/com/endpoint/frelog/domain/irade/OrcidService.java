package com.endpoint.frelog.domain.irade;

import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.exception.*;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.scheduling.annotation.Scheduled;
import java.time.*;
import java.util.*;
import java.security.*;
import java.nio.charset.StandardCharsets;

@Service
public class OrcidService {
    private final OrcidAttemptRepository attempts;
    private final UserRepository users;
    private final AuthService auth;
    private final Clock clock;
    public OrcidService(OrcidAttemptRepository attempts,UserRepository users,AuthService auth,Clock clock){this.attempts=attempts;this.users=users;this.auth=auth;this.clock=clock;}
    public static String random(){byte[] bytes=new byte[32];new SecureRandom().nextBytes(bytes);return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);}
    public static String hash(String value){try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8)));}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
    private LocalDateTime now(){return LocalDateTime.ofInstant(clock.instant(),ZoneOffset.UTC);}
    public record Start(String state,String browser){}
    @Transactional public Start begin(Long userId,Integer workspace){
        if(workspace!=null&&workspace<1)throw new CustomException(ErrorCode.INVALID_INPUT_VALUE);
        String state=random(),browser=random();var attempt=new OrcidAttempt();attempt.id=hash(state);attempt.browserHash=hash(browser);attempt.targetUserId=userId;attempt.workspaceId=workspace;attempt.expiresAt=now().plusMinutes(10);attempts.save(attempt);return new Start(state,browser);
    }
    /** Commit consumption before the remote token exchange; a callback cannot be replayed even after a provider failure. */
    @Transactional public OrcidAttempt consume(String state,String browser){
        if(state==null||browser==null||!state.matches("[A-Za-z0-9_-]{43}")||!browser.matches("[A-Za-z0-9_-]{43}"))throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        var attempt=attempts.lock(hash(state)).orElseThrow(()->new CustomException(ErrorCode.INVALID_CREDENTIALS));
        if(attempt.consumed||!attempt.expiresAt.isAfter(now())||!MessageDigest.isEqual(attempt.browserHash.getBytes(StandardCharsets.US_ASCII),hash(browser).getBytes(StandardCharsets.US_ASCII)))throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        attempt.consumed=true;return attempt;
    }
    public record Completed(LoginResponse login,String orcid,String issuer){}
    @Transactional public Completed complete(OrcidIdentity identity,Long target,HttpServletRequest request){
        var user=users.findByOrcidAndOrcidIssuer(identity.id(),identity.issuer()).orElse(null);
        if(target!=null){
            var current=users.lockById(target).orElseThrow(()->new CustomException(ErrorCode.INVALID_CREDENTIALS));
            if(!current.isActive()||com.endpoint.frelog.domain.canvas.bot.CanvasBotService.isBot(current))throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
            if(user!=null&&!user.getUserId().equals(target))throw new CustomException(ErrorCode.ACCESS_DENIED,"이 ORCID는 다른 계정에 연결되어 있습니다.");
            if(current.getOrcid()!=null&&(!current.getOrcid().equals(identity.id())||!Objects.equals(current.getOrcidIssuer(),identity.issuer())))throw new CustomException(ErrorCode.ACCESS_DENIED,"기존 ORCID 연결을 다른 식별자로 교체할 수 없습니다.");
            user=current;user.setOrcid(identity.id());user.setOrcidIssuer(identity.issuer());
        }else if(user==null){
            int tag=users.findMaxTagNumberByNicknameForUpdate(identity.name())+1;
            user=new User(null,null,identity.name(),tag,Role.ROLE_USER);user.setOrcid(identity.id());user.setOrcidIssuer(identity.issuer());
            user=users.saveAndFlush(user);
        }
        if(user.getStatus()==UserStatus.SUSPENDED)throw new CustomException(ErrorCode.USER_SUSPENDED);
        if(!user.isActive())throw new CustomException(ErrorCode.USER_WITHDRAWN);
        return new Completed(auth.completeOrcidLogin(user,request),identity.id(),identity.issuer());
    }
    public User require(CustomUserDetails current){
        if(current==null||current.getUserId()==null)throw new CustomException(ErrorCode.UNAUTHORIZED);
        if(!current.getUser().isActive()||current.getUser().getOrcid()==null)throw new CustomException(ErrorCode.ACCESS_DENIED,"IRADE에 ORCID를 연결하고 로그인하세요.");
        return current.getUser();
    }
    @Scheduled(fixedDelay=3600000) @Transactional public void cleanup(){attempts.deleteExpired(now().minusHours(1));}
}
