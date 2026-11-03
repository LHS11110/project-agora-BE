package com.endpoint.frelog.domain.auth.qr;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.mfa.MfaService;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.scheduling.annotation.Scheduled;
import java.time.*;
import java.util.*;
import java.net.URI;
import java.security.*;
import java.nio.charset.StandardCharsets;
@Service
public class QrLoginService {
 private final QrLoginRepository logins;private final UserRepository users;private final MfaService mfa;private final JwtTokenProvider tokens;private final Clock clock;private final SecureRandom random=new SecureRandom();final String origin;
 public QrLoginService(QrLoginRepository logins,UserRepository users,MfaService mfa,JwtTokenProvider tokens,Clock clock,@Value("${app.passkeys.origin:https://frelog.p-e.kr}") String origin){this.logins=logins;this.users=users;this.mfa=mfa;this.tokens=tokens;this.clock=clock;this.origin=origin;var uri=URI.create(origin);if(!"https".equals(uri.getScheme())||uri.getHost()==null||uri.getUserInfo()!=null||uri.getQuery()!=null||uri.getFragment()!=null||!uri.getPath().isEmpty())throw new IllegalArgumentException("QR login requires a fixed HTTPS origin");}
 private LocalDateTime now(){return LocalDateTime.ofInstant(clock.instant(),ZoneOffset.UTC);}
 static String hash(String value){try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8)));}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
 private String opaque(){byte[] bytes=new byte[32];random.nextBytes(bytes);return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);}
 private boolean matches(String value,String hashed){return value!=null&&value.length()<=128&&MessageDigest.isEqual(hash(value).getBytes(StandardCharsets.US_ASCII),hashed.getBytes(StandardCharsets.US_ASCII));}
 void checkOrigin(HttpServletRequest request){if(!origin.equals(request.getHeader("Origin")))throw new QrRejected("등록된 HTTPS 주소에서 다시 시도해주세요.");}
 private String device(String ua){if(ua==null)ua="";String browser=ua.contains("Edg/")?"Edge":ua.contains("Firefox/")?"Firefox":ua.contains("Chrome/")?"Chrome":ua.contains("Safari/")?"Safari":"Browser";String os=ua.contains("Android")?"Android":ua.contains("iPhone")?"iPhone":ua.contains("iPad")?"iPad":ua.contains("Windows")?"Windows":ua.contains("Mac")?"macOS":ua.contains("Linux")?"Linux":"Device";return browser+" / "+os;}
 public record Start(String requestId,String pollingToken,String qrUrl,String displayCode,int expiresInSeconds,int pollingIntervalSeconds){}
 record Created(Start response,String browserToken){}
 @Transactional public Created start(HttpServletRequest request){
  if(logins.countByRequesterIpAndCreatedAtAfter(request.getRemoteAddr(),now().minusMinutes(1))>=6)throw new QrRejected("QR 요청이 너무 많습니다. 잠시 후 다시 시도해주세요.");
  var q=new QrLogin();q.id=UUID.randomUUID().toString();String poll=opaque(),approve=opaque(),browser=opaque();q.pollHash=hash(poll);q.approveHash=hash(approve);q.browserHash=hash(browser);q.displayCode=String.format(Locale.ROOT,"%06d",random.nextInt(1000000));q.requesterIp=request.getRemoteAddr();q.deviceLabel=device(request.getHeader("User-Agent"));q.createdAt=now();q.expiresAt=now().plusMinutes(3);logins.save(q);
  return new Created(new Start(q.id,poll,origin+"/qr-login#request="+q.id+"&approval="+approve,q.displayCode,180,2),browser);
 }
 private QrLogin active(String id){var q=logins.lock(id).orElseThrow(()->new QrRejected("QR 코드가 만료되었거나 사용할 수 없습니다. 새 QR 코드를 만들어주세요."));if(!q.expiresAt.isAfter(now()))throw new QrRejected("QR 코드가 만료되었습니다. 새 QR 코드를 만들어주세요.");return q;}
 private QrLogin browser(String id,String poll,String cookie,HttpServletRequest request){var q=active(id);if(!matches(poll,q.pollHash)||!matches(cookie,q.browserHash)||!q.requesterIp.equals(request.getRemoteAddr()))throw new QrRejected("QR 요청 기기를 확인하지 못했습니다. 새 QR 코드를 만들어주세요.");return q;}
 public record Preview(String deviceLabel,String requesterIp,LocalDateTime expiresAt,String status){}
 @Transactional public Preview preview(String id,String approve){var q=active(id);if(!matches(approve,q.approveHash))throw new QrRejected("올바른 QR 코드가 아닙니다.");return new Preview(q.deviceLabel,q.requesterIp,q.expiresAt,q.status);}
 @Transactional(noRollbackFor=QrRejected.class) public void decide(String id,String approve,String code,boolean allow,Long userId,String jwt){
  var q=active(id);if(!matches(approve,q.approveHash)||!"PENDING".equals(q.status))throw new QrRejected("이미 처리되었거나 올바르지 않은 QR 코드입니다.");
  if(!allow){q.status="DENIED";return;}
  if(!Objects.equals(code,q.displayCode)){if(++q.failures>=5)q.status="DENIED";throw new QrRejected("새 기기의 확인 코드가 일치하지 않습니다.");}
  var user=users.lockById(userId).filter(u->u.isActive()).orElseThrow(()->new QrRejected("로그인할 수 없는 계정입니다."));
  if(!mfa.accepts(userId,jwt)||!Objects.equals(tokens.getUserIdFromToken(jwt),userId))throw new QrRejected("승인 기기에서 다시 로그인해주세요.");
  q.userId=user.getUserId();q.mfaVersion=tokens.getMfaVersion(jwt);q.status="APPROVED";
 }
 public record Poll(String status,LoginResponse login){}
 @Transactional public Poll poll(String id,String secret,String cookie,HttpServletRequest request){
  var q=browser(id,secret,cookie,request);
  if(q.lastPoll!=null&&q.lastPoll.plusSeconds(1).isAfter(now()))return new Poll("WAIT",null);q.lastPoll=now();
  if("APPROVED".equals(q.status)){
   var user=users.lockById(q.userId).filter(u->u.isActive()).orElseThrow(()->new QrRejected("로그인할 수 없는 계정입니다."));
   LoginResponse login=mfa.completeDeviceApproval(user,q.mfaVersion,request);q.status="USED";q.userId=null;q.mfaVersion=null;return new Poll("COMPLETE",login);
  }
  return new Poll(q.status,null);
 }
 @Transactional public void cancel(String id,String secret,String cookie,HttpServletRequest request){var q=browser(id,secret,cookie,request);if(!"USED".equals(q.status)){q.status="CANCELLED";q.userId=null;q.mfaVersion=null;}}
 @Scheduled(fixedDelay=60000) @Transactional public void cleanup(){logins.cleanup(now());}
}
