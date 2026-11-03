package com.endpoint.frelog.domain.auth.email;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.mail.javamail.JavaMailSenderImpl;
import org.springframework.test.util.ReflectionTestUtils;
import javax.net.ssl.*;
import java.net.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.KeyStore;
import java.util.*;
import java.util.concurrent.*;
import static org.assertj.core.api.Assertions.*;

/** Real local encrypted SMTP submission, with a disposable certificate and credentials. */
class VerificationMailerTlsTest {
 @TempDir Path directory;
 @ParameterizedTest @ValueSource(strings={"starttls","ssl"})
 void sendsUtf8CodeOverAuthenticatedTls(String mode) throws Exception {
  Path store=directory.resolve("fixture.p12");String password="fixture-pass";
  var process=new ProcessBuilder(Path.of(System.getProperty("java.home"),"bin/keytool").toString(),"-genkeypair","-alias","fixture","-keyalg","RSA","-keysize","2048","-validity","1","-dname","CN=localhost","-ext","SAN=ip:127.0.0.1,dns:localhost","-keystore",store.toString(),"-storetype","PKCS12","-storepass",password,"-keypass",password,"-noprompt").redirectErrorStream(true).start();process.getInputStream().readAllBytes();assertThat(process.waitFor()).isZero();
  KeyStore keys=KeyStore.getInstance("PKCS12");try(var in=Files.newInputStream(store)){keys.load(in,password.toCharArray());}
  var km=KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());km.init(keys,password.toCharArray());SSLContext tls=SSLContext.getInstance("TLS");tls.init(km.getKeyManagers(),null,null);
  ServerSocket server=mode.equals("ssl")?tls.getServerSocketFactory().createServerSocket():new ServerSocket();server.bind(new InetSocketAddress("127.0.0.1",0));server.setSoTimeout(15000);
  ExecutorService worker=Executors.newSingleThreadExecutor();
  try(server){
   Future<String> received=worker.submit(()->{
    Socket socket=server.accept();socket.setSoTimeout(15000);BufferedReader input=reader(socket);BufferedWriter output=writer(socket);reply(output,"220 localhost fixture");assertThat(input.readLine()).startsWith("EHLO ");
    if(mode.equals("starttls")){
     reply(output,"250-localhost\r\n250 STARTTLS");assertThat(input.readLine()).isEqualTo("STARTTLS");reply(output,"220 Begin TLS");
     SSLSocket encrypted=(SSLSocket)tls.getSocketFactory().createSocket(socket,"127.0.0.1",socket.getPort(),true);encrypted.setUseClientMode(false);encrypted.startHandshake();socket=encrypted;input=reader(socket);output=writer(socket);assertThat(input.readLine()).startsWith("EHLO ");
    }
    reply(output,"250-localhost\r\n250 AUTH PLAIN");String auth=input.readLine();assertThat(auth).startsWith("AUTH PLAIN");String payload=auth.length()>11?auth.substring(11).trim():null;
    if(payload==null||payload.isEmpty()){reply(output,"334 ");payload=input.readLine();}
    assertThat(new String(Base64.getDecoder().decode(payload),StandardCharsets.UTF_8)).endsWith("\0fixture-user\0fixture-password");reply(output,"235 Authenticated");
    assertThat(input.readLine()).startsWith("MAIL FROM:");reply(output,"250 Sender accepted");assertThat(input.readLine()).contains("recipient@example.invalid");reply(output,"250 Recipient accepted");assertThat(input.readLine()).isEqualTo("DATA");reply(output,"354 Send message");
    StringBuilder data=new StringBuilder();String line;while((line=input.readLine())!=null&&!line.equals("."))data.append(line.startsWith("..")?line.substring(1):line).append("\r\n");reply(output,"250 Queued");String quit=input.readLine();if("QUIT".equals(quit))reply(output,"221 Bye");socket.close();return data.toString();
   });
   var mailer=new VerificationMailer("127.0.0.1",server.getLocalPort(),"fixture-user","fixture-password","sender@example.invalid",mode);
   var sender=(JavaMailSenderImpl)ReflectionTestUtils.getField(mailer,"sender");assertThat(sender.getJavaMailProperties()).doesNotContainKey("mail.smtp.ssl.trust");
   // Explicitly trust this one disposable test endpoint only. Production never sets this.
   sender.getJavaMailProperties().setProperty("mail.smtp.ssl.trust","127.0.0.1");
   try { mailer.send("recipient@example.invalid","123456"); } catch (RuntimeException failure) { if(received.isDone())received.get();throw failure; }
   String raw=received.get(20,TimeUnit.SECONDS);
   var message=new jakarta.mail.internet.MimeMessage(jakarta.mail.Session.getInstance(new Properties()),new ByteArrayInputStream(raw.getBytes(StandardCharsets.ISO_8859_1)));
   assertThat(message.getSubject()).contains("FreLog");assertThat(message.getContent().toString()).contains("123456","10분");
  }finally{worker.shutdownNow();}
 }
 static BufferedReader reader(Socket socket) throws IOException{return new BufferedReader(new InputStreamReader(socket.getInputStream(),StandardCharsets.ISO_8859_1));}
 static BufferedWriter writer(Socket socket) throws IOException{return new BufferedWriter(new OutputStreamWriter(socket.getOutputStream(),StandardCharsets.ISO_8859_1));}
 static void reply(BufferedWriter output,String line) throws IOException{output.write(line);output.write("\r\n");output.flush();}
}
