package com.endpoint.frelog.domain.link;

import org.junit.jupiter.api.Test;
import java.io.ByteArrayInputStream;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import static org.junit.jupiter.api.Assertions.*;

class LinkPreviewControllerTest {
 @Test void extractsAndCleansMetadata() throws Exception {
  var result=LinkPreviewController.parse("<html><head><title>Fallback</title><meta property='og:title' content='Page title'><meta name='description' content='One &amp; two'></head></html>");
  assertEquals("Page title",result.get("title"));assertEquals("One & two",result.get("description"));
 }
 @Test void blocksPrivateAndReservedAddresses() throws Exception {
  for(String host:new String[]{"127.0.0.1","10.2.3.4","172.20.0.1","192.168.1.1","169.254.169.254","100.64.0.1","::1","203.0.113.1"})assertFalse(LinkPreviewController.publicV4(InetAddress.getByName(host)),host);
  assertTrue(LinkPreviewController.publicV4(InetAddress.getByName("8.8.8.8")));
 }
 @Test void boundsAndDecodesResponses() throws Exception {
  var chunked=new ByteArrayInputStream("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n".getBytes(StandardCharsets.US_ASCII));assertEquals("hello world",new String(LinkPreviewController.readBody(chunked,true,-1),StandardCharsets.UTF_8));
  assertEquals(3,LinkPreviewController.readBody(new ByteArrayInputStream(new byte[10]),false,3).length);
  assertEquals(262144,LinkPreviewController.readBody(new ByteArrayInputStream(new byte[300000]),false,-1).length);
 }
}
