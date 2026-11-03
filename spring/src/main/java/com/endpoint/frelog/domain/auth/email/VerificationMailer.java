package com.endpoint.frelog.domain.auth.email;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.mail.javamail.JavaMailSenderImpl;
import org.springframework.mail.SimpleMailMessage;
import org.springframework.stereotype.Component;
import java.nio.charset.StandardCharsets;

@Component
public class VerificationMailer {
 private final JavaMailSenderImpl sender;
 private final String from;
 private final boolean configured;
 public VerificationMailer(@Value("${app.email.host:}") String host,@Value("${app.email.port:587}") int port,
  @Value("${app.email.username:}") String username,@Value("${app.email.password:}") String password,
  @Value("${app.email.from:}") String from,@Value("${app.email.security:starttls}") String security){
  if(!security.equals("starttls")&&!security.equals("ssl"))throw new IllegalArgumentException("MAIL_SECURITY must be starttls or ssl");
  this.from=from;sender=new JavaMailSenderImpl();sender.setHost(host);sender.setPort(port);sender.setUsername(username);sender.setPassword(password);sender.setDefaultEncoding(StandardCharsets.UTF_8.name());
  var props=sender.getJavaMailProperties();props.setProperty("mail.smtp.auth","true");props.setProperty("mail.smtp.ssl.checkserveridentity","true");props.setProperty("mail.smtp.ssl.protocols","TLSv1.3 TLSv1.2");
  props.setProperty("mail.smtp.starttls.enable",String.valueOf(security.equals("starttls")));props.setProperty("mail.smtp.starttls.required",String.valueOf(security.equals("starttls")));props.setProperty("mail.smtp.ssl.enable",String.valueOf(security.equals("ssl")));
  props.setProperty("mail.smtp.connectiontimeout","5000");props.setProperty("mail.smtp.timeout","10000");props.setProperty("mail.smtp.writetimeout","10000");
  boolean valid=false;try{var address=new jakarta.mail.internet.InternetAddress(from,true);address.validate();valid=address.getAddress()!=null&&address.getAddress().contains("@");}catch(jakarta.mail.internet.AddressException ignored){}
  configured=!host.isBlank()&&port>0&&port<=65535&&!username.isBlank()&&!password.isBlank()&&valid;
 }
 public boolean available(){return configured;}
 public void send(String email,String code){
  var message=new SimpleMailMessage();message.setFrom(from);message.setTo(email);message.setSubject("[FreLog] 회원가입 이메일 인증 코드");
  message.setText("FreLog 회원가입을 위한 이메일 인증 코드입니다.\n\n인증 코드: "+code+"\n\n10분 안에 가입 화면에 입력해주세요.\n요청하지 않았다면 이 메일을 무시하셔도 됩니다. 코드를 다른 사람에게 전달하지 마세요.\n\nFreLog\nhttps://frelog.p-e.kr\n");sender.send(message);
 }
}
