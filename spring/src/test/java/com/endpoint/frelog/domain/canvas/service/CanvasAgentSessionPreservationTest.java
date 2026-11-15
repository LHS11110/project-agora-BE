package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocumentJson;
import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.global.config.ElasticsearchProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.web.client.RestClient;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.mock;
class CanvasAgentSessionPreservationTest {
 @Test void sessionHistorySurvivesFullDocumentSaveWithoutEnteringRestResponses() throws Exception {
  ObjectMapper mapper=new ObjectMapper();
  JsonNode source=mapper.readTree("{\"canvas-id\":7,\"canvas-name\":\"Canvas\",\"items\":{},\"agent-sessions\":{\"version\":1,\"messages\":{\"session\":[{\"role\":\"user\",\"content\":\"private shared prompt\"}]}}}");
  CanvasDocument document=mapper.treeToValue(source,CanvasDocument.class);
  assertFalse(mapper.valueToTree(document).has("agent-sessions"));
  assertFalse(mapper.writeValueAsString(document).contains("private shared prompt"));
  assertEquals(source.get("agent-sessions"),CanvasDocumentJson.toStorageNode(mapper,document,DocumentScope.CANVAS).get("agent-sessions"));
  CanvasElasticsearchService service=new CanvasElasticsearchService(mock(RestClient.class),new ElasticsearchProperties(),mapper);
  String encoded=ReflectionTestUtils.invokeMethod(service,"encodeCanvasDocument",document);
  JsonNode stored=mapper.readTree(encoded);
  assertTrue(stored.get("agent-sessions-b64").isArray());
  assertFalse(stored.has("agent-sessions"));
  CanvasDocument restored=ReflectionTestUtils.invokeMethod(service,"decodeCanvasDocument",stored);
  restored.setCanvasName("Renamed");
  assertFalse(mapper.valueToTree(restored).has("agent-sessions-b64"));
  String again=ReflectionTestUtils.invokeMethod(service,"encodeCanvasDocument",restored);
  assertEquals(stored.get("agent-sessions-b64"),mapper.readTree(again).get("agent-sessions-b64"));
 }
}
