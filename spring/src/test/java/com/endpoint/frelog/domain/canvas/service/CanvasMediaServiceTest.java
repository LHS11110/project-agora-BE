package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.mock.web.MockMultipartFile;
import org.springframework.web.multipart.MultipartFile;
import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.Map;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasMediaServiceTest {
 @TempDir Path directory;
 private static byte[] pngBytes(int width,int height){
  return new byte[]{
   (byte)0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a,
   0,0,0,13,'I','H','D','R',
   (byte)(width>>>24),(byte)(width>>>16),(byte)(width>>>8),(byte)width,
   (byte)(height>>>24),(byte)(height>>>16),(byte)(height>>>8),(byte)height,
   8,6,0,0,0,0,0,0,0
  };
 }
 private static byte[] bmpBytes(int dibSize,int width,int height){
  byte[] bytes=new byte[30];
  bytes[0]='B';bytes[1]='M';
  bytes[14]=(byte)dibSize;bytes[15]=(byte)(dibSize>>>8);bytes[16]=(byte)(dibSize>>>16);bytes[17]=(byte)(dibSize>>>24);
  if(dibSize==12){
   bytes[18]=(byte)width;bytes[19]=(byte)(width>>>8);
   bytes[20]=(byte)height;bytes[21]=(byte)(height>>>8);
  }else{
   bytes[18]=(byte)width;bytes[19]=(byte)(width>>>8);bytes[20]=(byte)(width>>>16);bytes[21]=(byte)(width>>>24);
   bytes[22]=(byte)height;bytes[23]=(byte)(height>>>8);bytes[24]=(byte)(height>>>16);bytes[25]=(byte)(height>>>24);
  }
  return bytes;
 }
 @Test void preservesOriginalGifBytesAndReturnsCanvasScopedResource()throws Exception{
  var resources=mock(CanvasResourceService.class);when(resources.getCanvasDirectory(7)).thenReturn(directory);var service=new CanvasMediaService(resources);
  byte[] gif={'G','I','F','8','9','a',2,0,2,0,0,0,0,59};var result=service.save(7,new MockMultipartFile("file","anim.gif","image/gif",gif));assertEquals("gif",result.get("mediaType"));String id=result.get("url").substring(result.get("url").lastIndexOf('/')+1);assertArrayEquals(gif,Files.readAllBytes(service.path(7,id)));assertEquals("image/gif",CanvasMediaService.contentType(id));
  assertThrows(CustomException.class,()->service.path(7,"../../private"));assertThrows(CustomException.class,()->service.save(7,new MockMultipartFile("file","fake.gif","image/gif",new byte[]{1,2,3})));
 }
 @Test void savesStaticImagesWithMagicByteSniffingAndImageMediaType()throws Exception{
  var resources=mock(CanvasResourceService.class);when(resources.getCanvasDirectory(7)).thenReturn(directory);var service=new CanvasMediaService(resources);
  byte[] png=pngBytes(2,2);
  byte[] jpg=new byte[]{(byte)0xff,(byte)0xd8,(byte)0xff,(byte)0xe0,0,16,'J','F','I','F'};
  byte[] webp=new byte[]{'R','I','F','F',20,0,0,0,'W','E','B','P','V','P','8',' '};
  byte[] bmp=bmpBytes(40,2,2);
  byte[] bmpTopDown=bmpBytes(40,4,-3);
  byte[] avifDirect=new byte[]{0,0,0,16,'f','t','y','p','a','v','i','f',0,0,0,0};
  byte[] avifMif1=new byte[]{0,0,0,24,'f','t','y','p','m','i','f','1',0,0,0,0,'m','i','f','1','a','v','i','f'};
  Map<String,byte[]> samples=Map.of(
   "png|image/png",png,
   "jpg|image/jpeg",jpg,
   "webp|image/webp",webp,
   "bmp|image/bmp",bmp,
   "avif|image/avif",avifDirect
  );
  for(var entry:samples.entrySet()){
   String[] parts=entry.getKey().split("\\|");
   String ext=parts[0],expectedMime=parts[1];
   var result=service.save(7,new MockMultipartFile("file","upload."+ext,expectedMime,entry.getValue()));
   assertEquals("image",result.get("mediaType"));
   assertTrue(result.get("url").endsWith("."+ext));
   String id=result.get("url").substring(result.get("url").lastIndexOf('/')+1);
   assertArrayEquals(entry.getValue(),Files.readAllBytes(service.path(7,id)));
   assertEquals(expectedMime,CanvasMediaService.contentType(id));
  }
  assertEquals("bmp",CanvasMediaService.format(bmpTopDown));
  assertEquals("avif",CanvasMediaService.format(avifMif1));
 }
 @Test void rejectsUnsafeOrUnsupportedImagePayloadsAndOversizedImages()throws Exception{
  var resources=mock(CanvasResourceService.class);when(resources.getCanvasDirectory(7)).thenReturn(directory);var service=new CanvasMediaService(resources);
  assertThrows(CustomException.class,()->CanvasMediaService.format("<svg xmlns=\"http://www.w3.org/2000/svg\"/>".getBytes(StandardCharsets.UTF_8)));
  assertThrows(CustomException.class,()->CanvasMediaService.format("BM fake plain text payload with enough bytes".getBytes(StandardCharsets.UTF_8)));
  assertThrows(CustomException.class,()->CanvasMediaService.format(pngBytes(20000,20000)));
  assertThrows(CustomException.class,()->CanvasMediaService.format(new byte[]{0,0,0,16,'f','t','y','p','h','e','i','c',0,0,0,0}));
  MultipartFile oversizedPng=mock(MultipartFile.class);
  when(oversizedPng.isEmpty()).thenReturn(false);
  when(oversizedPng.getSize()).thenReturn(CanvasMediaService.MAX_IMAGE+1);
  when(oversizedPng.getInputStream()).thenReturn(new ByteArrayInputStream(pngBytes(2,2)));
  CustomException error=assertThrows(CustomException.class,()->service.save(7,oversizedPng));
  assertEquals(ErrorCode.PAYLOAD_TOO_LARGE,error.getErrorCode());
 }
 @Test void sniffsContainerAndRejectsHugeGifDimensions(){assertEquals("mp4",CanvasMediaService.format(new byte[]{0,0,0,20,'f','t','y','p','m','p','4','2'}));assertEquals("webm",CanvasMediaService.format(new byte[]{0x1a,0x45,(byte)0xdf,(byte)0xa3}));assertThrows(CustomException.class,()->CanvasMediaService.format(new byte[]{'G','I','F','8','9','a',(byte)255,(byte)255,(byte)255,(byte)255}));}
}
