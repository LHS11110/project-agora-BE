package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.global.exception.CustomException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.mock.web.MockMultipartFile;
import java.nio.file.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasMediaServiceTest {
 @TempDir Path directory;
 @Test void preservesOriginalGifBytesAndReturnsCanvasScopedResource()throws Exception{
  var resources=mock(CanvasResourceService.class);when(resources.getCanvasDirectory(7)).thenReturn(directory);var service=new CanvasMediaService(resources);
  byte[] gif={'G','I','F','8','9','a',2,0,2,0,0,0,0,59};var result=service.save(7,new MockMultipartFile("file","anim.gif","image/gif",gif));assertEquals("gif",result.get("mediaType"));String id=result.get("url").substring(result.get("url").lastIndexOf('/')+1);assertArrayEquals(gif,Files.readAllBytes(service.path(7,id)));assertEquals("image/gif",CanvasMediaService.contentType(id));
  assertThrows(CustomException.class,()->service.path(7,"../../private"));assertThrows(CustomException.class,()->service.save(7,new MockMultipartFile("file","fake.gif","image/gif",new byte[]{1,2,3})));
 }
 @Test void sniffsContainerAndRejectsHugeGifDimensions(){assertEquals("mp4",CanvasMediaService.format(new byte[]{0,0,0,20,'f','t','y','p','m','p','4','2'}));assertEquals("webm",CanvasMediaService.format(new byte[]{0x1a,0x45,(byte)0xdf,(byte)0xa3}));assertThrows(CustomException.class,()->CanvasMediaService.format(new byte[]{'G','I','F','8','9','a',(byte)255,(byte)255,(byte)255,(byte)255}));}
}
