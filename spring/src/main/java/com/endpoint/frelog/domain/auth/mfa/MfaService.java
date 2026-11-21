package com.endpoint.frelog.domain.auth.mfa;

import com.endpoint.frelog.domain.auth.dto.*;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.scheduling.annotation.Scheduled;
import java.time.*;
import java.util.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import javax.crypto.*;
import javax.crypto.spec.*;
import jakarta.servlet.http.HttpServletRequest;

@Service
public class MfaService {
    private final MfaStateRepository states;
    private final MfaChallengeRepository challenges;
    private final UserRepository users;
    private final UserSessionRepository sessions;
    private final JwtTokenProvider tokens;
    private com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions;
    @org.springframework.beans.factory.annotation.Autowired void setAuthSessions(com.endpoint.frelog.domain.auth.session.AuthSessionService authSessions){this.authSessions=authSessions;}
    private final byte[] encryptionKey;
    private final SecureRandom random=new SecureRandom();
    private final Clock clock;
    public MfaService(MfaStateRepository states,MfaChallengeRepository challenges,UserRepository users,UserSessionRepository sessions,JwtTokenProvider tokens,
                      @Value("${app.mfa.encryption-key:}") String key,Clock clock){
        this.states=states;this.challenges=challenges;this.users=users;this.sessions=sessions;this.tokens=tokens;this.clock=clock;
        encryptionKey=key.isBlank()?null:Base64.getDecoder().decode(key);if(encryptionKey!=null&&encryptionKey.length!=32)throw new IllegalArgumentException("MFA encryption key must be 32 bytes (base64)");
    }
    private LocalDateTime now(){return LocalDateTime.ofInstant(clock.instant(),ZoneOffset.UTC);}
    private long step(){return clock.instant().getEpochSecond()/30;}
    private String randomToken(int size){byte[] bytes=new byte[size];random.nextBytes(bytes);return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);}
    private static String hash(String value){try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8)));}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
    private void available(){if(encryptionKey==null)throw new MfaRejected("MFA 서버 설정이 준비되지 않았습니다.");}
    private String crypt(Long id,String value,boolean encrypt){
        available();try{var cipher=Cipher.getInstance("AES/GCM/NoPadding");byte[] iv=new byte[12];byte[] input;
            if(encrypt){random.nextBytes(iv);input=value.getBytes(StandardCharsets.US_ASCII);}else{byte[] data=Base64.getDecoder().decode(value);System.arraycopy(data,0,iv,0,12);input=Arrays.copyOfRange(data,12,data.length);}
            cipher.init(encrypt?Cipher.ENCRYPT_MODE:Cipher.DECRYPT_MODE,new SecretKeySpec(encryptionKey,"AES"),new GCMParameterSpec(128,iv));cipher.updateAAD(("FreLog:TOTP:"+id).getBytes(StandardCharsets.US_ASCII));byte[] output=cipher.doFinal(input);
            if(!encrypt)return new String(output,StandardCharsets.US_ASCII);byte[] result=new byte[iv.length+output.length];System.arraycopy(iv,0,result,0,12);System.arraycopy(output,0,result,12,output.length);return Base64.getEncoder().encodeToString(result);
        }catch(GeneralSecurityException|IllegalArgumentException e){throw new IllegalStateException("MFA secret encryption failed",e);}
    }
    @Transactional(readOnly=true) public boolean enabled(Long id){return states.findById(id).map(s->s.enabled).orElse(false);}
    @Transactional(readOnly=true) public boolean accepts(Long id,String token){return acceptsVersion(id,tokens.getMfaVersion(token));}
    @Transactional(readOnly=true) public boolean acceptsVersion(Long id,String version){return states.findById(id).map(s->!s.enabled||(s.version!=null&&Objects.equals(s.version,version))).orElse(true);}
    @Transactional(readOnly=true) public Map<String,Object> status(Long id){var s=states.findById(id).orElse(null);return Map.of("available",encryptionKey!=null,"enabled",s!=null&&s.enabled,"recoveryCodesRemaining",s==null||s.recoveryHashes==null||s.recoveryHashes.isEmpty()?0:s.recoveryHashes.split(",").length);}
    private void recent(String token){var date=tokens.getIssuedAt(token);if(date==null||date.toInstant().isBefore(clock.instant().minusSeconds(600)))throw new MfaRejected("보안 설정을 변경하려면 다시 로그인해주세요.");}
    private MfaState setupState(Long id){users.lockById(id).orElseThrow(()->new MfaRejected("계정을 찾을 수 없습니다."));return states.lock(id).orElseGet(()->states.saveAndFlush(new MfaState(id)));}
    @Transactional public Map<String,String> begin(Long id,String token){available();recent(token);var s=setupState(id);if(s.enabled)throw new MfaRejected("MFA가 이미 활성화되어 있습니다.");
        // A locked enrollment cannot bypass rate limiting by asking for a new seed.
        checkLock(s);byte[] bytes=new byte[20];random.nextBytes(bytes);String secret=Totp.base32(bytes);s.pendingSecret=crypt(id,secret,true);s.pendingExpires=now().plusMinutes(10);
        var account=users.findById(id).orElseThrow();String email=account.getEmail()!=null?account.getEmail():account.getOrcid()!=null?account.getOrcid():account.getNickname();String label=java.net.URLEncoder.encode("FreLog:"+email,StandardCharsets.UTF_8).replace("+","%20");return Map.of("secret",secret,"uri","otpauth://totp/"+label+"?secret="+secret+"&issuer=FreLog&algorithm=SHA1&digits=6&period=30");}
    private void checkLock(MfaState s){if(s.lockedUntil!=null&&s.lockedUntil.isAfter(now()))throw new MfaRejected("인증 시도가 너무 많습니다. 5분 후 다시 시도해주세요.");if(s.lockedUntil!=null){s.lockedUntil=null;s.failures=0;}}
    private void reject(MfaState s){s.failures++;if(s.failures>=5)s.lockedUntil=now().plusMinutes(5);throw new MfaRejected("코드가 올바르지 않거나 이미 사용되었습니다. 새 코드를 확인해주세요.");}
    private List<String> recovery(MfaState s){var plain=new ArrayList<String>();var hashes=new ArrayList<String>();for(int i=0;i<10;i++){byte[] b=new byte[10];random.nextBytes(b);String value=Totp.base32(b);plain.add(value.substring(0,4)+"-"+value.substring(4,8)+"-"+value.substring(8,12)+"-"+value.substring(12));hashes.add(hash(s.userId+":"+value));}s.recoveryHashes=String.join(",",hashes);return plain;}
    private void verify(MfaState s,String code){checkLock(s);String value=code==null?"":code.trim().toUpperCase(Locale.ROOT).replace("-","").replace(" ","");
        long matched=Totp.match(crypt(s.userId,s.secret,false),value,step(),s.lastStep);
        if(matched>=0)s.lastStep=matched;
        else if(value.matches("[A-Z2-7]{16}")&&s.recoveryHashes!=null){var hashes=new ArrayList<>(Arrays.asList(s.recoveryHashes.split(",")));if(!hashes.remove(hash(s.userId+":"+value)))reject(s);s.recoveryHashes=String.join(",",hashes);}
        else reject(s);s.failures=0;s.lockedUntil=null;
    }
    public record Enrollment(LoginResponse login,List<String> recoveryCodes){}
    @Transactional(noRollbackFor=MfaRejected.class) public Enrollment confirm(Long id,String code,String token,HttpServletRequest request){available();recent(token);var s=setupState(id);checkLock(s);if(s.enabled||s.pendingSecret==null||s.pendingExpires==null||!s.pendingExpires.isAfter(now()))throw new MfaRejected("등록 시간이 만료되었습니다. 다시 시작해주세요.");
        long match=Totp.match(crypt(id,s.pendingSecret,false),code,step(),-1);if(match<0)reject(s);s.secret=s.pendingSecret;s.pendingSecret=null;s.pendingExpires=null;s.enabled=true;s.version=UUID.randomUUID().toString();s.lastStep=match;s.failures=0;s.lockedUntil=null;var codes=recovery(s);return new Enrollment(issue(users.findById(id).orElseThrow(),s.version,request),codes);}
    @Transactional public void cancel(Long id){var s=states.lock(id).orElse(null);if(s!=null){s.pendingSecret=null;s.pendingExpires=null;}}
    @Transactional(noRollbackFor=MfaRejected.class) public void disable(Long id,String code){var s=states.lock(id).orElseThrow(()->new MfaRejected("MFA가 활성화되어 있지 않습니다."));if(!s.enabled)throw new MfaRejected("MFA가 활성화되어 있지 않습니다.");verify(s,code);s.enabled=false;s.secret=null;s.pendingSecret=null;s.pendingExpires=null;s.recoveryHashes=null;s.version=null;s.lastStep=-1;}
    @Transactional(noRollbackFor=MfaRejected.class) public List<String> regenerate(Long id,String code){var s=states.lock(id).orElseThrow(()->new MfaRejected("MFA가 활성화되어 있지 않습니다."));if(!s.enabled)throw new MfaRejected("MFA가 활성화되어 있지 않습니다.");verify(s,code);return recovery(s);}
    @Transactional public LoginResponse challenge(User user,HttpServletRequest request){available();var s=states.findById(user.getUserId()).orElseThrow();String raw=randomToken(32);var c=new MfaChallenge();c.id=hash(raw);c.userId=user.getUserId();c.version=s.version;c.clientIp=request.getRemoteAddr();c.expiresAt=now().plusMinutes(5);challenges.save(c);return LoginResponse.challenge(raw);}
    @Transactional(noRollbackFor=MfaRejected.class) public LoginResponse complete(String raw,String code,HttpServletRequest request){
        available();if(raw==null||raw.length()>128)throw new MfaRejected("MFA 로그인이 만료되었습니다. 다시 로그인해주세요.");var c=challenges.lock(hash(raw)).orElseThrow(()->new MfaRejected("MFA 로그인이 만료되었습니다. 다시 로그인해주세요."));
        if(c.used||!c.expiresAt.isAfter(now())||!Objects.equals(c.clientIp,request.getRemoteAddr()))throw new MfaRejected("MFA 로그인이 만료되었습니다. 다시 로그인해주세요.");User u=users.findById(c.userId).orElseThrow();var s=states.lock(c.userId).orElseThrow();if(!s.enabled||!Objects.equals(c.version,s.version))throw new MfaRejected("MFA 설정이 변경되었습니다. 다시 로그인해주세요.");
        if(!u.isActive())throw new MfaRejected("로그인할 수 없는 계정입니다.");verify(s,code);c.used=true;return issue(u,s.version,request);
    }
    /** Only called after server-side WebAuthn signature and REQUIRED user verification succeed. */
    @Transactional public LoginResponse completeVerifiedPasskey(User user,HttpServletRequest request){
        if(!user.isActive())throw new MfaRejected("로그인할 수 없는 계정입니다.");
        var state=states.lock(user.getUserId()).orElse(null);
        // Bind the token to the current MFA revision so the ordinary JWT filter
        // still rejects old sessions after MFA is re-enrolled.
        return issue(user,state!=null&&state.enabled?state.version:null,request);
    }
    /** A logged-in device explicitly approved this new device under the same MFA revision. */
    @Transactional public LoginResponse completeDeviceApproval(User user,String approvedVersion,HttpServletRequest request){
        if(!user.isActive())throw new MfaRejected("로그인할 수 없는 계정입니다.");
        var state=states.lock(user.getUserId()).orElse(null);
        if(state!=null&&state.enabled&&(state.version==null||!Objects.equals(state.version,approvedVersion)))throw new MfaRejected("MFA 설정이 변경되었습니다. QR 로그인을 다시 시작해주세요.");
        return issue(user,state!=null&&state.enabled?state.version:null,request);
    }
    private LoginResponse issue(User u,String version,HttpServletRequest request){var session=sessions.findByIdWithPessimisticLock(u.getUserId()).orElseGet(()->new UserSession(u));session.updateLastLogin(now());sessions.save(session);return LoginResponse.of(authSessions.issue(u,request,version),UserResponse.from(u));}
    @Scheduled(fixedDelay=3600000) @Transactional public void cleanup(){challenges.deleteExpired(now().minusDays(1));}
}
