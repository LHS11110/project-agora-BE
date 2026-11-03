package com.endpoint.frelog.domain.auth.email;

import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.*;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.mail.MailException;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.security.*;
import java.nio.charset.StandardCharsets;
import java.time.*;
import java.util.*;

@Service
public class EmailVerificationService {
 private static final org.slf4j.Logger log=org.slf4j.LoggerFactory.getLogger(EmailVerificationService.class);
 private final EmailProofRepository proofs;
 private final EmailQuotaRepository quotas;
 private final UserRepository users;
 private final VerificationMailer mailer;
 private final Clock clock;
 private final byte[] key;
 private final SecureRandom random=new SecureRandom();
 public EmailVerificationService(EmailProofRepository proofs,EmailQuotaRepository quotas,UserRepository users,VerificationMailer mailer,Clock clock,@Value("${app.email.verification-secret:}") String secret){
  this.proofs=proofs;this.quotas=quotas;this.users=users;this.mailer=mailer;this.clock=clock;key=secret.isBlank()?null:Base64.getDecoder().decode(secret);if(key!=null&&key.length!=32)throw new IllegalArgumentException("Email verification key must be 32 bytes (base64)");
 }
 public static String normalize(String email){return email.trim().toLowerCase(Locale.ROOT);}
 private LocalDateTime now(){return LocalDateTime.ofInstant(clock.instant(),ZoneOffset.UTC);}
 public boolean available(){return key!=null&&mailer.available();}
 private String opaque(){byte[] b=new byte[32];random.nextBytes(b);return Base64.getUrlEncoder().withoutPadding().encodeToString(b);}
 private static String hash(String value){try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8)));}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
 private String codeHash(String challenge,String code){try{Mac mac=Mac.getInstance("HmacSHA256");mac.init(new SecretKeySpec(key,"HmacSHA256"));return HexFormat.of().formatHex(mac.doFinal((challenge+":"+code).getBytes(StandardCharsets.US_ASCII)));}catch(GeneralSecurityException e){throw new IllegalStateException(e);}}
 private boolean equal(String a,String b){return a!=null&&b!=null&&MessageDigest.isEqual(a.getBytes(StandardCharsets.US_ASCII),b.getBytes(StandardCharsets.US_ASCII));}
 public record Requested(String challengeId,int expiresInSeconds,int resendAfterSeconds){}
 public record Verified(String emailVerificationToken,int expiresInSeconds){}
 @Transactional(noRollbackFor=EmailVerificationRejected.class,isolation=Isolation.SERIALIZABLE)
 public Requested request(String address,String ip){
  if(!available())throw new EmailVerificationRejected(ErrorCode.EMAIL_DELIVERY_UNAVAILABLE);
  String email=normalize(address);var now=now();String ipId=hash("email-quota:"+ip);
  var q=quotas.lock(ipId).orElseGet(()->quotas.saveAndFlush(new EmailQuota(ipId,now)));
  if(q.windowStart.isBefore(now.minusHours(1))){q.windowStart=now;q.sends=0;}
  if(q.sends>=20)throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_RATE_LIMIT);
  q.sends++;
  // Lock proof before looking at users: signup uses the same proof -> user order.
  var p=proofs.lock(email).orElseGet(()->{var fresh=new EmailProof(email);fresh.windowStart=now;fresh.expiresAt=now;return proofs.saveAndFlush(fresh);});
  // Existing accounts receive an explicit notice, never a dummy mail challenge.
  if(users.existsByEmail(email))throw new com.endpoint.frelog.global.exception.CustomException(ErrorCode.EMAIL_ALREADY_EXISTS);
  if(p.lastSentAt!=null&&p.lastSentAt.plusSeconds(60).isAfter(now))throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_RATE_LIMIT);
  if(p.windowStart==null||p.windowStart.isBefore(now.minusHours(1))){p.windowStart=now;p.sends=0;}
  if(p.sends>=5)throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_RATE_LIMIT);
  p.sends++;p.lastSentAt=now;String challenge=opaque();String code=String.format(Locale.ROOT,"%06d",random.nextInt(1_000_000));p.challengeHash=hash(challenge);p.codeHash=codeHash(challenge,code);p.expiresAt=now.plusMinutes(10);p.attempts=0;p.grantHash=null;p.grantExpiresAt=null;p.consumed=false;
  try{mailer.send(email,code);}catch(MailException failure){log.warn("Verification SMTP send failed: {}",failure.getClass().getSimpleName());p.codeHash=null;throw new EmailVerificationRejected(ErrorCode.EMAIL_DELIVERY_UNAVAILABLE);}
  return new Requested(challenge,600,60);
 }
 @Transactional(noRollbackFor=EmailVerificationRejected.class)
 public Verified verify(String address,String challenge,String code){
  if(key==null)throw new EmailVerificationRejected(ErrorCode.EMAIL_DELIVERY_UNAVAILABLE);
  if(challenge==null||challenge.length()>128||code==null||!code.matches("[0-9]{6}"))throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_INVALID);
  var p=proofs.lock(normalize(address)).orElseThrow(()->new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_INVALID));
  if(!equal(p.challengeHash,hash(challenge))||p.codeHash==null||p.expiresAt==null||!p.expiresAt.isAfter(now()))throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_INVALID);
  if(p.attempts>=5)throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_RATE_LIMIT);
  p.attempts++;if(!equal(p.codeHash,codeHash(challenge,code)))throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_INVALID);
  String grant=opaque();p.codeHash=null;p.grantHash=hash(grant);p.grantExpiresAt=now().plusMinutes(10);p.consumed=false;return new Verified(grant,600);
 }
 /** Joins the signup transaction: proof consumption and account creation commit together. */
 @Transactional public void consume(String address,String grant){
  if(grant==null||grant.isBlank()||grant.length()>128)throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_REQUIRED);
  var p=proofs.lock(normalize(address)).orElseThrow(()->new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_REQUIRED));
  if(p.consumed||p.grantHash==null||!equal(p.grantHash,hash(grant))||p.grantExpiresAt==null||!p.grantExpiresAt.isAfter(now()))throw new EmailVerificationRejected(ErrorCode.EMAIL_VERIFICATION_REQUIRED);
  p.consumed=true;p.grantHash=null;
 }
 @Scheduled(fixedDelay=3600000) @Transactional public void cleanup(){var cutoff=now().minusHours(2);proofs.cleanup(cutoff);quotas.cleanup(cutoff);}
}
