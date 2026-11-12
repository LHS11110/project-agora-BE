package com.endpoint.frelog.domain.link;

import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import javax.net.ssl.*;
import javax.swing.text.*;
import javax.swing.text.html.*;
import javax.swing.text.html.parser.ParserDelegator;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.*;

@RestController
@RequestMapping("/api/link-preview")
public class LinkPreviewController {
 private static final int LIMIT=262144;
 public record PreviewRequest(String url){}
 @PostMapping public ResponseEntity<Map<String,String>> preview(@RequestBody PreviewRequest request){
  String url=request==null?null:request.url();
  try{return ResponseEntity.ok().cacheControl(CacheControl.maxAge(Duration.ofMinutes(10)).cachePrivate()).body(fetch(url));}
  catch(Exception ignored){return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(Map.of());}
 }
 static boolean publicV4(InetAddress address){
  if(!(address instanceof Inet4Address)||address.isAnyLocalAddress()||address.isLoopbackAddress()||address.isLinkLocalAddress()||address.isSiteLocalAddress()||address.isMulticastAddress())return false;
  byte[] b=address.getAddress();int a=b[0]&255,c=b[1]&255,d=b[2]&255;
  return a>0&&a<224&&!(a==100&&c>=64&&c<=127)&&!(a==192&&c==0)&&!(a==192&&c==88&&d==99)&&!(a==198&&(c==18||c==19||c==51&&d==100))&&!(a==203&&c==0&&d==113);
 }
 static Map<String,String> fetch(String input)throws Exception{
  if(input==null||input.length()>2048)throw new IOException();URI uri=URI.create(input);
  for(int redirects=0;redirects<=3;redirects++){
   String host=uri.getHost();int port=uri.getPort();if(!"https".equalsIgnoreCase(uri.getScheme())||host==null||uri.getUserInfo()!=null||(port!=-1&&port!=443)||host.length()>253)throw new IOException();
   InetAddress[] addresses=InetAddress.getAllByName(host);if(addresses.length==0)throw new IOException();
   InetAddress address=Arrays.stream(addresses).filter(LinkPreviewController::publicV4).findFirst().orElseThrow(IOException::new);
   try(Socket plain=new Socket()){
    plain.connect(new InetSocketAddress(address,443),3000);plain.setSoTimeout(4000);
    try(SSLSocket socket=(SSLSocket)((SSLSocketFactory)SSLSocketFactory.getDefault()).createSocket(plain,host,443,true)){
     SSLParameters params=socket.getSSLParameters();params.setEndpointIdentificationAlgorithm("HTTPS");socket.setSSLParameters(params);socket.startHandshake();
     String path=uri.getRawPath();if(path==null||path.isEmpty())path="/";if(uri.getRawQuery()!=null)path+="?"+uri.getRawQuery();if(path.indexOf('\r')>=0||path.indexOf('\n')>=0)throw new IOException();
     socket.getOutputStream().write(("GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUser-Agent: FreLog-LinkPreview/1.0\r\nAccept: text/html\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
     InputStream in=socket.getInputStream();String status=line(in,8192);String[] parts=status.split(" ",3);int code=Integer.parseInt(parts[1]);Map<String,String> headers=new HashMap<>();int size=0;String l;
     while(!(l=line(in,8192)).isEmpty()){size+=l.length();if(size>32768)throw new IOException();int colon=l.indexOf(':');if(colon>0)headers.put(l.substring(0,colon).trim().toLowerCase(Locale.ROOT),l.substring(colon+1).trim());}
     if(code>=300&&code<400&&headers.containsKey("location")){uri=uri.resolve(headers.get("location"));continue;}
     if(code!=200||!headers.getOrDefault("content-type","").toLowerCase(Locale.ROOT).contains("text/html"))return Map.of();
     long contentLength=-1;try{contentLength=Long.parseLong(headers.getOrDefault("content-length","-1"));}catch(NumberFormatException ignored){}byte[] body=readBody(in,headers.getOrDefault("transfer-encoding","").toLowerCase(Locale.ROOT).contains("chunked"),contentLength);return parse(new String(body,StandardCharsets.UTF_8));
    }
   }
  }throw new IOException();
 }
 static String line(InputStream in,int max)throws IOException{ByteArrayOutputStream out=new ByteArrayOutputStream();while(out.size()<max){int c=in.read();if(c<0)throw new EOFException();if(c=='\n'){byte[] b=out.toByteArray();int n=b.length;if(n>0&&b[n-1]=='\r')n--;return new String(b,0,n,StandardCharsets.ISO_8859_1);}out.write(c);}throw new IOException();}
 static byte[] readBody(InputStream in,boolean chunked,long contentLength)throws IOException{ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[4096];if(!chunked){int n;int max=contentLength<0?LIMIT:(int)Math.min(LIMIT,contentLength);while(out.size()<max&&(n=in.read(buffer,0,Math.min(buffer.length,max-out.size())))>0)out.write(buffer,0,n);return out.toByteArray();}while(out.size()<LIMIT){String h=line(in,128).split(";",2)[0].trim();int length=Integer.parseInt(h,16);if(length<=0)break;int remaining=Math.min(length,LIMIT-out.size());while(remaining>0){int n=in.read(buffer,0,Math.min(buffer.length,remaining));if(n<0)throw new EOFException();out.write(buffer,0,n);remaining-=n;}if(out.size()>=LIMIT)break;line(in,4);}return out.toByteArray();}
 static Map<String,String> parse(String html)throws IOException{
  Map<String,String> data=new HashMap<>();StringBuilder title=new StringBuilder();boolean[] inTitle={false};
  new ParserDelegator().parse(new StringReader(html),new HTMLEditorKit.ParserCallback(){
   @Override public void handleStartTag(HTML.Tag tag,MutableAttributeSet a,int pos){if(tag==HTML.Tag.TITLE)inTitle[0]=true;else if(tag==HTML.Tag.META)meta(a);}
   @Override public void handleSimpleTag(HTML.Tag tag,MutableAttributeSet a,int pos){if(tag==HTML.Tag.META)meta(a);}
   @Override public void handleEndTag(HTML.Tag tag,int pos){if(tag==HTML.Tag.TITLE)inTitle[0]=false;}
   @Override public void handleText(char[] text,int pos){if(inTitle[0]&&title.length()<512)title.append(text);}
   void meta(MutableAttributeSet a){Object name=a.getAttribute(HTML.Attribute.NAME);if(name==null)name=a.getAttribute("property");Object content=a.getAttribute(HTML.Attribute.CONTENT);if(name!=null&&content!=null){String key=name.toString().toLowerCase(Locale.ROOT);if(Set.of("og:title","og:description","twitter:title","twitter:description","description").contains(key))data.putIfAbsent(key,content.toString());}}
  },true);
  String name=clean(data.getOrDefault("og:title",data.getOrDefault("twitter:title",title.toString())),200),description=clean(data.getOrDefault("og:description",data.getOrDefault("twitter:description",data.getOrDefault("description",""))),350);
  Map<String,String> result=new HashMap<>();if(!name.isEmpty())result.put("title",name);if(!description.isEmpty())result.put("description",description);return result;
 }
 static String clean(String s,int max){s=s.replaceAll("[\\p{Cntrl}\\s]+"," ").trim();return s.length()>max?s.substring(0,max):s;}
}
