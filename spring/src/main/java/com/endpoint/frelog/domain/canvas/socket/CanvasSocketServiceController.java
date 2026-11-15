package com.endpoint.frelog.domain.canvas.socket;

import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.service.*;
import com.endpoint.frelog.domain.canvas.controller.*;
import com.endpoint.frelog.domain.canvas.bot.CanvasBotService;
import com.endpoint.frelog.domain.canvas.github.CanvasGithubService;
import com.endpoint.frelog.domain.canvas.terminal.CanvasTerminalSocket;
import com.endpoint.frelog.domain.user.controller.UserProfileController;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.fasterxml.jackson.databind.*;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.*;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;
import org.springframework.web.server.ResponseStatusException;
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/** Only the authenticated broker calls this private, explicitly enumerated service interface. */
@RestController
@RequestMapping("/internal/canvas/socket")
public class CanvasSocketServiceController {
 private final CanvasService canvases;private final CanvasResourceService resources;private final CanvasMediaController media;private final CanvasPdfController pdf;private final CanvasController canvasController;private final CanvasBotService bots;private final CanvasGithubService github;private final UserProfileController profiles;private final CanvasTerminalSocket terminal;private final UserRepository users;private final com.endpoint.frelog.domain.link.LinkPreviewController links;private final ObjectMapper json;private final byte[] internalToken;
 private final Map<String,Upload> uploads=new ConcurrentHashMap<>();
 private static final int CHUNK=96*1024;
 private static final long TTL=600_000;
 private static class Upload {DocumentScope scope;long user;int canvas;String path;Path directory;List<JsonNode> files;long[] offsets;volatile long expires=System.currentTimeMillis()+TTL;volatile Object result;}
 public CanvasSocketServiceController(CanvasService canvases,CanvasResourceService resources,CanvasMediaController media,CanvasPdfController pdf,CanvasController canvasController,CanvasBotService bots,CanvasGithubService github,UserProfileController profiles,CanvasTerminalSocket terminal,UserRepository users,ObjectMapper json,com.endpoint.frelog.domain.link.LinkPreviewController links,@Value("${CPP_INTERNAL_API_TOKEN:}") String token){this.canvases=canvases;this.resources=resources;this.media=media;this.pdf=pdf;this.canvasController=canvasController;this.bots=bots;this.github=github;this.profiles=profiles;this.terminal=terminal;this.users=users;this.json=json;this.links=links;this.internalToken=token.getBytes(StandardCharsets.UTF_8);}
 private static ResponseStatusException fail(HttpStatus status,String reason){return new ResponseStatusException(status,reason);}
 @PostMapping public Object dispatch(@RequestHeader(value="X-Agora-Internal-Token",defaultValue="") String supplied,@RequestBody Map<String,Object> input)throws IOException{return exchange(supplied,json.valueToTree(input));}
 public Object exchange(String supplied,JsonNode input)throws IOException{
  if(internalToken.length<32||!MessageDigest.isEqual(internalToken,supplied.getBytes(StandardCharsets.UTF_8)))throw fail(HttpStatus.UNAUTHORIZED,"Internal authentication required");
  if(!input.path("canvasId").canConvertToInt()||input.path("canvasId").asInt()<=0||!input.path("userId").canConvertToLong())throw fail(HttpStatus.BAD_REQUEST,"Invalid canvas identity");
  final DocumentScope scope;
  try{scope=DocumentScope.fromWire(input.path("scope").asText("canvas"));}
  catch(IllegalArgumentException e){throw fail(HttpStatus.BAD_REQUEST,"Invalid document scope");}
  int id=input.path("canvasId").asInt();long uid=input.path("userId").asLong();
  var account=users.findById(uid).filter(u->u.isActive()).orElseThrow(()->fail(HttpStatus.UNAUTHORIZED,"Inactive account"));var user=new CustomUserDetails(account);
  var request=input.path("request");String operation=request.path("operation").asText("api"),path=request.path("path").asText(),method=request.path("method").asText("GET");
  if(path.length()>2048||path.contains("?")||path.contains("#"))throw fail(HttpStatus.BAD_REQUEST,"Invalid service path");
  var document=scope==DocumentScope.CANVAS
   ?canvases.getCanvasItemDocument(id,user,"socket-service-access")
   :canvases.getItemDocument(scope,id,user,"socket-service-access");
  String capability=path.equals("/runtime/terminal/ticket")?"execute":(operation.equals("resource")||operation.equals("api")&&(method.equals("GET")||path.startsWith("/github")||path.equals("/link-preview")||path.equals("/access")))?"read":"write";
  if(!CanvasGroupPermissions.allows(document,user,capability))throw fail(HttpStatus.FORBIDDEN,"Canvas permission denied");
  if(operation.equals("upload_begin"))return begin(scope,id,uid,path,request);
  if(operation.equals("upload_chunk")||operation.equals("upload_finish")||operation.equals("upload_cancel")){
   String key=request.path("uploadId").asText();Upload upload=uploads.get(key);
   if(upload==null||upload.scope!=scope||upload.canvas!=id||upload.user!=uid||upload.expires<System.currentTimeMillis())throw fail(HttpStatus.GONE,"Upload expired");
   synchronized(upload){upload.expires=System.currentTimeMillis()+TTL;
    if(operation.equals("upload_cancel")){uploads.remove(key,upload);delete(upload);return Map.of("cancelled",true);}
    if(operation.equals("upload_finish")){
     if(upload.result!=null)return upload.result;
     for(int i=0;i<upload.files.size();i++)if(upload.offsets[i]!=upload.files.get(i).path("size").asLong())throw fail(HttpStatus.CONFLICT,"Upload is incomplete");
     MultipartFile main=null,thumbnail=null;for(int i=0;i<upload.files.size();i++){JsonNode file=upload.files.get(i);var part=new DiskPart(upload.directory.resolve("part"+i),file.path("field").asText(),file.path("name").asText(),file.path("type").asText());if(file.path("field").asText().equals("thumbnail"))thumbnail=part;else main=part;}
     if(scope==DocumentScope.WORKSPACE&&upload.path.equals("/image")){
      upload.result=Map.of("image",canvases.updateRepresentativeImage(DocumentScope.WORKSPACE,id,main,user));
     }else if(scope==DocumentScope.WORKSPACE){
      throw fail(HttpStatus.NOT_FOUND,"Unsupported workspace upload");
     }else{
      upload.result=switch(upload.path){case "/media"->media.upload(id,main,user).getBody();case "/pdfs"->pdf.upload(id,main,thumbnail,user).getBody();case "/image"->canvasController.updateCanvasImage(id,main,user).getBody();default->throw fail(HttpStatus.BAD_REQUEST,"Unsupported upload");};
     }
     delete(upload);return upload.result;
    }
    if(upload.result!=null)throw fail(HttpStatus.CONFLICT,"Upload is already complete");
    int index=request.path("index").asInt(-1);long offset=request.path("offset").asLong(-1);String encoded=request.path("data").asText();
    if(index<0||index>=upload.files.size()||offset<0||encoded.length()>CHUNK*4/3+8)throw fail(HttpStatus.BAD_REQUEST,"Invalid upload chunk");
    byte[] bytes;try{bytes=Base64.getDecoder().decode(encoded);}catch(IllegalArgumentException e){throw fail(HttpStatus.BAD_REQUEST,"Invalid base64 chunk");}
    if(bytes.length==0||bytes.length>CHUNK||offset+bytes.length>upload.files.get(index).path("size").asLong())throw fail(HttpStatus.BAD_REQUEST,"Chunk exceeds declared size");
    Path part=upload.directory.resolve("part"+index);
    try(var file=new RandomAccessFile(part.toFile(),"rw")){
     if(offset<upload.offsets[index]){byte[] existing=new byte[bytes.length];file.seek(offset);file.readFully(existing);if(!Arrays.equals(existing,bytes))throw fail(HttpStatus.CONFLICT,"Chunk retry differs from saved data");}
     else{if(offset!=upload.offsets[index])throw fail(HttpStatus.CONFLICT,"Chunk offset mismatch");file.seek(offset);file.write(bytes);upload.offsets[index]+=bytes.length;}
    }
    return Map.of("offset",upload.offsets[index]);
   }
  }
  if(operation.equals("resource")){
   if(path.matches("/linked-resource/[1-9][0-9]*/(media/.*|pdfs/.*|image)")){
    if(scope!=DocumentScope.CANVAS||CanvasBotService.isBot(account))throw fail(HttpStatus.FORBIDDEN,"Bots cannot read another canvas");
    String[] parts=path.split("/",4);int source=Integer.parseInt(parts[2]);
    var sourceDocument=canvases.getCanvasItemDocument(source,user,"socket-resource-access");
    if(!CanvasGroupPermissions.allows(sourceDocument,user,"read"))throw fail(HttpStatus.FORBIDDEN,"Source canvas read permission required");
    return resource(scope,source,"/"+parts[3],request,user);
   }
   return resource(scope,id,path,request,user);
  }
  if(!operation.equals("api"))throw fail(HttpStatus.BAD_REQUEST,"Unknown canvas operation");
  JsonNode body=request.path("body");
  if(scope==DocumentScope.CANVAS){
   if(path.equals("/bots")){if(method.equals("GET"))return bots.list(id,user);if(method.equals("POST"))return bots.create(id,body.path("name").asText(),user);}
   if(path.matches("/bots/[1-9][0-9]*(/rotate)?")){long bot=Long.parseLong(path.split("/")[2]);if(method.equals("DELETE")&&!path.endsWith("/rotate")){bots.revoke(id,bot,user);return Map.of();}if(method.equals("POST")&&path.endsWith("/rotate"))return bots.rotate(id,bot,user);}
   if(path.equals("/github")&&method.equals("GET"))return github.context(id,user);
  }
  if(method.equals("POST")&&path.equals("/link-preview"))return links.preview(new com.endpoint.frelog.domain.link.LinkPreviewController.PreviewRequest(body.path("url").asText())).getBody();
  if(method.equals("POST")&&path.equals("/access")){
   var original=((org.springframework.web.context.request.ServletRequestAttributes)org.springframework.web.context.request.RequestContextHolder.currentRequestAttributes()).getRequest();
   var forwarded=new jakarta.servlet.http.HttpServletRequestWrapper(original){@Override public String getRemoteAddr(){return input.path("clientIp").asText();}};
   return scope==DocumentScope.CANVAS
    ?canvases.accessCanvas(id,forwarded,user,body.path("password").asText(null),body.path("canvas_password_token").asText(null))
    :canvases.access(scope,id,forwarded,user,body.path("password").asText(null),body.path("canvas_password_token").asText(null));
  }
  if(path.equals("/people")&&(method.equals("POST")||method.equals("DELETE"))){
   var handle=json.treeToValue(body,com.endpoint.frelog.domain.canvas.dto.CanvasUpdateDtos.ParticipantHandleRequest.class);
   if(scope==DocumentScope.CANVAS){
    if(method.equals("POST"))canvasController.addParticipant(id,handle,user);else canvasController.removeParticipant(id,handle,user);
   }else{
    if(method.equals("POST"))canvases.addParticipant(scope,id,handle.nickname(),handle.tagNumber(),user);
    else canvases.removeParticipant(scope,id,handle.nickname(),handle.tagNumber(),user);
   }
   return Map.of();
  }
  if(path.isEmpty()&&method.equals("DELETE")){
   if(scope==DocumentScope.CANVAS)canvasController.deleteCanvas(id,user);else canvases.delete(scope,id,user);
   return Map.of();
  }
  if(method.equals("POST")){
   if(scope==DocumentScope.CANVAS){
    if(path.equals("/github/bind"))return github.bind(id,json.treeToValue(body,CanvasGithubService.Bind.class),user);
    if(path.equals("/github/branches"))return github.checkoutResponse(id,json.treeToValue(body,CanvasGithubService.BranchRequest.class),user);
    if(path.equals("/github/initialized")){github.initialized(id,json.treeToValue(body,CanvasGithubService.BranchRequest.class),user);return Map.of();}
    if(path.equals("/github/permissions"))return github.policy(id,json.treeToValue(body,CanvasGithubService.Policy.class),user);
    if(path.equals("/github/authorize"))return github.authorize(id,json.treeToValue(body,CanvasGithubService.Action.class),user);
   }
   if(path.equals("/runtime/terminal/ticket"))return terminal.issue(scope,id,body.path("objectId").asText(),body.path("passwordGrant").asText(null),request.path("authorization").asText(),input.path("clientIp").asText(),user);
  }
  if(method.equals("GET")&&path.startsWith("/profiles/")){String[] parts=path.split("/");if(parts.length==4)return profiles.profile(java.net.URLDecoder.decode(parts[2],StandardCharsets.UTF_8),Integer.valueOf(parts[3]),user);}
  if(method.equals("GET")&&path.equals(""))return scope==DocumentScope.CANVAS?canvases.getCanvasSummary(id,user):canvases.getSummary(scope,id,user);
  if(method.equals("GET")&&path.equals("/settings"))return scope==DocumentScope.CANVAS?canvases.getCanvasSettings(id,user):canvases.getSettings(scope,id,user);
  throw fail(HttpStatus.BAD_REQUEST,"Unsupported canvas service operation");
 }
 private synchronized Object begin(DocumentScope scope,int id,long uid,String path,JsonNode request)throws IOException{
  if(!Set.of("/media","/pdfs","/image").contains(path))throw fail(HttpStatus.BAD_REQUEST,"Unsupported upload");
  cleanup();if(uploads.size()>=128||uploads.values().stream().filter(u->u.result==null).count()>=8||uploads.values().stream().filter(u->u.user==uid&&u.result==null).count()>=2)throw fail(HttpStatus.TOO_MANY_REQUESTS,"Too many uploads");
  JsonNode files=request.path("files");if(!files.isArray()||files.isEmpty()||files.size()>2)throw fail(HttpStatus.BAD_REQUEST,"Invalid upload files");
  Upload upload=new Upload();upload.scope=scope;upload.user=uid;upload.canvas=id;upload.path=path;upload.files=new ArrayList<>();Set<String> fields=new HashSet<>();
  for(JsonNode file:files){String field=file.path("field").asText();long size=file.path("size").asLong(-1);long max=field.equals("thumbnail")?2L*1024*1024:path.equals("/pdfs")?5L*1024*1024:path.equals("/image")?2L*1024*1024:100L*1024*1024;
   if(!fields.add(field)||(!field.equals(path.equals("/image")?"image":"file")&&!(path.equals("/pdfs")&&field.equals("thumbnail")))||size<=0||size>max||file.path("name").asText().length()>512||file.path("type").asText().length()>128)throw fail(HttpStatus.BAD_REQUEST,"Invalid upload file metadata");upload.files.add(file.deepCopy());}
  if(!fields.contains(path.equals("/image")?"image":"file"))throw fail(HttpStatus.BAD_REQUEST,"Main file missing");
  Path directory=scope==DocumentScope.CANVAS?resources.getCanvasDirectory(id):resources.getDirectory(scope,id);Files.createDirectories(directory);upload.directory=Files.createTempDirectory(directory,"socket-upload-");upload.offsets=new long[upload.files.size()];String key=UUID.randomUUID().toString();uploads.put(key,upload);return Map.of("uploadId",key,"chunkSize",CHUNK);
 }
 private Object resource(DocumentScope scope,int id,String path,JsonNode request,CustomUserDetails user)throws IOException{
  ResponseEntity<?> response;
  if(scope==DocumentScope.WORKSPACE){
   if(path.equals("/image")){
    var imagePath=resources.getRepresentativeImageFile(DocumentScope.WORKSPACE,id);
    if(imagePath==null||!Files.isRegularFile(imagePath))throw fail(HttpStatus.NOT_FOUND,"Resource not found");
    response=ResponseEntity.ok().contentType(MediaTypeFactory.getMediaType(new org.springframework.core.io.FileSystemResource(imagePath)).orElse(MediaType.APPLICATION_OCTET_STREAM)).body(new org.springframework.core.io.FileSystemResource(imagePath));
   }else if(path.matches("/profile-images/[1-9][0-9]*")){
    response=profiles.image(Long.valueOf(path.substring(16)),user);
   }else{
    throw fail(HttpStatus.NOT_FOUND,"Unsupported workspace resource");
   }
  }else{
   if(path.matches("/media/[0-9a-f-]+\\.(gif|mp4|webm|ogv)"))response=media.read(id,path.substring(7),user);
   else if(path.matches("/pdfs/[0-9a-f-]+(/thumbnail)?")){String key=path.split("/")[2];response=path.endsWith("/thumbnail")?pdf.thumbnail(id,key,user):pdf.read(id,key,user);}
   else if(path.equals("/image"))response=canvasController.getCanvasImage(id,user);
   else if(path.matches("/profile-images/[1-9][0-9]*"))response=profiles.image(Long.valueOf(path.substring(16)),user);
   else throw fail(HttpStatus.BAD_REQUEST,"Unsupported canvas resource");
  }
  if(!response.getStatusCode().is2xxSuccessful())throw fail(HttpStatus.NOT_FOUND,"Resource not found");
  long offset=request.path("offset").asLong(0);Object value=response.getBody();byte[] bytes;long size;String version;
  if(value instanceof org.springframework.core.io.Resource resource){size=resource.contentLength();version=resource.lastModified()+":"+size;if(request.has("version")&&!request.path("version").asText().equals(version))throw fail(HttpStatus.CONFLICT,"Resource changed during transfer");if(offset<0||offset>size)throw fail(HttpStatus.BAD_REQUEST,"Invalid resource offset");try(InputStream in=resource.getInputStream()){in.skipNBytes(offset);bytes=in.readNBytes(CHUNK);}}
  else if(value instanceof byte[] data){size=data.length;version=Arrays.hashCode(data)+":"+size;if(request.has("version")&&!request.path("version").asText().equals(version))throw fail(HttpStatus.CONFLICT,"Resource changed during transfer");if(offset<0||offset>size)throw fail(HttpStatus.BAD_REQUEST,"Invalid resource offset");bytes=Arrays.copyOfRange(data,(int)offset,(int)Math.min(size,offset+CHUNK));}
  else throw fail(HttpStatus.NOT_FOUND,"Resource not found");
  return Map.of("version",version,"data",Base64.getEncoder().encodeToString(bytes),"offset",offset,"size",size,"type",Optional.ofNullable(response.getHeaders().getContentType()).orElse(MediaType.APPLICATION_OCTET_STREAM).toString());
 }
 @Scheduled(fixedDelay=60000) public void cleanup(){uploads.forEach((key,u)->{synchronized(u){if(u.expires<System.currentTimeMillis()&&uploads.remove(key,u))delete(u);}});}
 private void delete(Upload upload){try{if(upload.directory!=null&&Files.exists(upload.directory)){try(var files=Files.list(upload.directory)){for(Path file:files.toList())Files.deleteIfExists(file);}Files.deleteIfExists(upload.directory);}}catch(IOException ignored){}}
 private record DiskPart(Path path,String field,String filename,String type) implements MultipartFile {
  public String getName(){return field;}public String getOriginalFilename(){return filename;}public String getContentType(){return type;}public boolean isEmpty(){return getSize()==0;}public long getSize(){try{return Files.size(path);}catch(IOException e){return 0;}}public byte[] getBytes()throws IOException{return Files.readAllBytes(path);}public InputStream getInputStream()throws IOException{return Files.newInputStream(path);}public void transferTo(File dest)throws IOException{Files.copy(path,dest.toPath(),StandardCopyOption.REPLACE_EXISTING);}
 }
}
