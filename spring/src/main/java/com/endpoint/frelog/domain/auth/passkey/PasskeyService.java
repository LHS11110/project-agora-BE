package com.endpoint.frelog.domain.auth.passkey;

import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import com.yubico.webauthn.*;
import com.yubico.webauthn.data.*;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.scheduling.annotation.Scheduled;
import java.net.URI;
import java.time.*;
import java.util.*;
import java.security.*;
import java.nio.charset.StandardCharsets;

@Service
public class PasskeyService {
    private final PasskeyCredentialRepository keys;
    private final PasskeyChallengeRepository challenges;
    private final UserRepository users;
    private final JwtTokenProvider tokens;
    private final AuthService auth;
    private final Clock clock;
    private final SecureRandom random=new SecureRandom();
    private final RelyingParty rp;
    final String origin;
    public PasskeyService(PasskeyCredentialRepository keys,PasskeyChallengeRepository challenges,UserRepository users,
                          JwtTokenProvider tokens,AuthService auth,Clock clock,PasskeyStore store,
                          @Value("${app.passkeys.rp-id:frelog.p-e.kr}") String rpId,
                          @Value("${app.passkeys.origin:https://frelog.p-e.kr}") String origin) {
        this.keys=keys;this.challenges=challenges;this.users=users;this.tokens=tokens;this.auth=auth;this.clock=clock;this.origin=origin;
        URI uri=URI.create(origin);
        if(!"https".equals(uri.getScheme())||!rpId.equals(uri.getHost())||uri.getRawUserInfo()!=null||uri.getRawQuery()!=null||uri.getRawFragment()!=null||!uri.getRawPath().isEmpty())
            throw new IllegalArgumentException("Passkey origin must be an HTTPS origin matching RP ID");
        rp=RelyingParty.builder().identity(RelyingPartyIdentity.builder().id(rpId).name("FreLog").build())
                .credentialRepository(store).origins(Set.of(origin)).allowOriginPort(false).allowOriginSubdomain(false).build();
    }
    static String hash(String value){try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8)));}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
    private LocalDateTime now(){return LocalDateTime.ofInstant(clock.instant(),ZoneOffset.UTC);}
    private String randomToken(){byte[] b=new byte[32];random.nextBytes(b);return Base64.getUrlEncoder().withoutPadding().encodeToString(b);}
    private User active(Long id){return users.lockById(id).filter(User::isActive).orElseThrow(()->new PasskeyRejected("로그인할 수 없는 계정입니다."));}
    private void recent(String token){var issued=tokens.getIssuedAt(token);if(issued==null||issued.toInstant().isBefore(clock.instant().minusSeconds(600)))throw new PasskeyRejected("보안 설정을 변경하려면 다시 로그인해주세요.");}
    void checkOrigin(HttpServletRequest request){if(!origin.equals(request.getHeader("Origin")))throw new PasskeyRejected("등록된 HTTPS 주소에서 다시 시도해주세요.");}
    public record KeyInfo(String id,String name,LocalDateTime createdAt,LocalDateTime lastUsedAt){}
    @Transactional(readOnly=true) public List<KeyInfo> list(Long id){return keys.findByUserIdOrderByCreatedAtAsc(id).stream().map(k->new KeyInfo(k.id,k.name,k.createdAt,k.lastUsedAt)).toList();}
    public record Options(String challengeToken,String optionsJson){}
    private Options saveChallenge(String purpose,Long userId,String requestJson,String optionsJson,HttpServletRequest request){
        if(challenges.countByClientIpAndCreatedAtAfter(request.getRemoteAddr(),now().minusMinutes(1))>=20)throw new PasskeyRejected("요청이 너무 많습니다. 잠시 후 다시 시도해주세요.");
        String raw=randomToken();var c=new PasskeyChallenge();c.id=hash(raw);c.userId=userId;c.purpose=purpose;c.clientIp=request.getRemoteAddr();c.requestJson=requestJson;c.createdAt=now();c.expiresAt=now().plusMinutes(5);challenges.save(c);return new Options(raw,optionsJson);
    }
    @Transactional public Options registerOptions(Long id,String token,HttpServletRequest request){
        recent(token);var user=active(id);var existing=keys.findByUserIdOrderByCreatedAtAsc(id);
        if(existing.size()>=10)throw new PasskeyRejected("패스키는 최대 10개까지 등록할 수 있습니다.");
        String handle=existing.isEmpty()?randomToken():existing.getFirst().userHandle;
        var options=rp.startRegistration(StartRegistrationOptions.builder()
                .user(UserIdentity.builder().name(id.toString()).displayName(user.getNickname()).id(PasskeyStore.bytes(handle)).build())
                .authenticatorSelection(AuthenticatorSelectionCriteria.builder().residentKey(ResidentKeyRequirement.REQUIRED).userVerification(UserVerificationRequirement.REQUIRED).build())
                .timeout(60000).build());
        try{return saveChallenge("REGISTER",id,options.toJson(),options.toCredentialsCreateJson(),request);}catch(java.io.IOException e){throw new IllegalStateException(e);}
    }
    @Transactional public Options loginOptions(HttpServletRequest request){
        var options=rp.startAssertion(StartAssertionOptions.builder().userVerification(UserVerificationRequirement.REQUIRED).timeout(60000).build());
        try{return saveChallenge("LOGIN",null,options.toJson(),options.toCredentialsGetJson(),request);}catch(java.io.IOException e){throw new IllegalStateException(e);}
    }
    private PasskeyChallenge consume(String raw,String cookie,String purpose,Long id,HttpServletRequest request){
        if(raw==null||cookie==null||!MessageDigest.isEqual(raw.getBytes(StandardCharsets.UTF_8),cookie.getBytes(StandardCharsets.UTF_8)))throw new PasskeyRejected("패스키 요청이 만료되었습니다. 다시 시도해주세요.");
        var c=challenges.lock(hash(raw)).orElseThrow(()->new PasskeyRejected("패스키 요청이 만료되었습니다. 다시 시도해주세요."));
        if(c.used||!c.expiresAt.isAfter(now())||!c.purpose.equals(purpose)||!Objects.equals(c.userId,id)||!c.clientIp.equals(request.getRemoteAddr()))throw new PasskeyRejected("패스키 요청이 만료되었습니다. 다시 시도해주세요.");
        c.used=true;return c;
    }
    @Transactional(noRollbackFor=PasskeyRejected.class)
    public void register(Long id,String token,String raw,String cookie,String credential,String name,HttpServletRequest request){
        recent(token);var c=consume(raw,cookie,"REGISTER",id,request);active(id);
        if(keys.findByUserIdOrderByCreatedAtAsc(id).size()>=10)throw new PasskeyRejected("패스키는 최대 10개까지 등록할 수 있습니다.");
        try {
            var options=PublicKeyCredentialCreationOptions.fromJson(c.requestJson);
            var existing=keys.findByUserIdOrderByCreatedAtAsc(id);
            if(!existing.isEmpty()&&!existing.getFirst().userHandle.equals(options.getUser().getId().getBase64Url()))throw new PasskeyRejected("등록 요청이 변경되었습니다. 다시 시도해주세요.");
            var result=rp.finishRegistration(FinishRegistrationOptions.builder().request(options).response(PublicKeyCredential.parseRegistrationResponseJson(credential)).build());
            if(!result.isUserVerified())throw new PasskeyRejected("기기의 사용자 인증이 필요합니다.");
            var p=new PasskeyCredential();p.id=UUID.randomUUID().toString();p.userId=id;p.userHandle=options.getUser().getId().getBase64Url();
            p.credentialId=result.getKeyId().getId().getBase64Url();p.credentialHash=hash(p.credentialId);p.publicKey=result.getPublicKeyCose().getBase64Url();p.signatureCount=result.getSignatureCount();
            p.name=name==null||name.isBlank()?"패스키":name.trim();p.createdAt=now();keys.save(p);
        }catch(PasskeyRejected e){throw e;}catch(Exception e){throw new PasskeyRejected("패스키를 등록하지 못했습니다. 다시 시도해주세요.");}
    }
    @Transactional(noRollbackFor=PasskeyRejected.class)
    public LoginResponse login(String raw,String cookie,String credential,HttpServletRequest request){
        var c=consume(raw,cookie,"LOGIN",null,request);
        try {
            var response=PublicKeyCredential.parseAssertionResponseJson(credential);
            var key=keys.lockCredential(hash(response.getId().getBase64Url())).orElseThrow(()->new PasskeyRejected("패스키 인증에 실패했습니다. 다른 로그인 방식을 사용해주세요."));
            var result=rp.finishAssertion(FinishAssertionOptions.builder().request(AssertionRequest.fromJson(c.requestJson)).response(response).build());
            if(!result.isSuccess()||!result.isUserVerified()||!key.userId.toString().equals(result.getUsername()))throw new PasskeyRejected("패스키 인증에 실패했습니다. 다시 시도해주세요.");
            var user=active(key.userId);key.signatureCount=result.getSignatureCount();key.lastUsedAt=now();
            return auth.completePasskeyLogin(user,request);
        }catch(PasskeyRejected e){throw e;}catch(Exception e){throw new PasskeyRejected("패스키 인증에 실패했습니다. 다시 시도해주세요.");}
    }
    @Transactional public void delete(Long userId,String id,String token){recent(token);active(userId);var p=keys.findById(id).filter(k->k.userId.equals(userId)).orElseThrow(()->new PasskeyRejected("패스키를 찾을 수 없습니다."));keys.delete(p);}
    @Scheduled(fixedDelay=3600000) @Transactional public void cleanup(){challenges.deleteExpired(now().minusHours(1));}
}
