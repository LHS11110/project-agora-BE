package com.endpoint.frelog.domain.canvas.controller;
import com.endpoint.frelog.domain.canvas.service.*;
import com.endpoint.frelog.global.exception.*;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.core.io.*;
import org.springframework.http.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;
import java.nio.file.Files;
import java.util.Map;
@RestController
@RequestMapping("/api/canvases/{canvasId}/media")
public class CanvasMediaController {
 private final CanvasService canvases;private final CanvasMediaService media;
 public CanvasMediaController(CanvasService canvases,CanvasMediaService media){this.canvases=canvases;this.media=media;}
 private void access(Integer id,CustomUserDetails user,String capability){var doc=canvases.getCanvasItemDocument(id,user,"media-access");if(!CanvasGroupPermissions.allows(doc,user,capability))throw new CustomException(ErrorCode.ACCESS_DENIED);}
 @PostMapping(consumes=MediaType.MULTIPART_FORM_DATA_VALUE)
 public ResponseEntity<Map<String,String>> upload(@PathVariable Integer canvasId,@RequestPart("file") MultipartFile file,@AuthenticationPrincipal CustomUserDetails user){access(canvasId,user,"write");return ResponseEntity.status(HttpStatus.CREATED).body(media.save(canvasId,file));}
 @GetMapping("/{id}")
 public ResponseEntity<Resource> read(@PathVariable Integer canvasId,@PathVariable String id,@AuthenticationPrincipal CustomUserDetails user){access(canvasId,user,"read");var path=media.path(canvasId,id);if(!Files.isRegularFile(path))return ResponseEntity.notFound().build();return ResponseEntity.ok().contentType(MediaType.parseMediaType(CanvasMediaService.contentType(id))).header(HttpHeaders.CACHE_CONTROL,"private, no-store").header("X-Content-Type-Options","nosniff").header(HttpHeaders.CONTENT_DISPOSITION,"inline; filename=\""+id+"\"").body(new FileSystemResource(path));}
}
