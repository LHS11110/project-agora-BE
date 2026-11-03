package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.junit.jupiter.api.Test;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasGroupPermissionsTest {
 CustomUserDetails user(long id){var u=mock(CustomUserDetails.class);when(u.getUserId()).thenReturn(id);return u;}
 CanvasDocument doc(){var d=new CanvasDocument("Test",1,1L,null,"default");d.setPeople(List.of(1L,2L));d.setInnerGroup(Map.of("admin-group",List.of(1L),"default",List.of(2L),"operators",List.of(2L)));return d;}
 @Test void legacyRightsAndOwnerArePreserved(){var d=doc();assertTrue(CanvasGroupPermissions.allows(d,user(2),"write"));d.setGroupPermissions(Map.of("default",Map.of("read",false,"write",false,"execute",false),"operators",Map.of("read",false,"write",false,"execute",false)));assertTrue(CanvasGroupPermissions.allows(d,user(1),"execute"));assertFalse(CanvasVisibility.allows(d,user(2)));}
 @Test void independentRightsAndGroupUnion(){var d=doc();d.setGroupPermissions(Map.of("default",Map.of("read",true,"write",false,"execute",false),"operators",Map.of("read",true,"write",false,"execute",true)));assertTrue(CanvasVisibility.allows(d,user(2)));assertFalse(CanvasGroupPermissions.allows(d,user(2),"write"));assertTrue(CanvasGroupPermissions.allows(d,user(2),"execute"));assertFalse(CanvasVisibility.allows(d,user(3)));}
 @Test void publicAccessUsesInitialGroupAndRevocationIsImmediate(){var d=doc();d.setVisibility("public");d.setGroupPermissions(Map.of("default",Map.of("read",true,"write",false,"execute",false)));assertTrue(CanvasVisibility.allows(d,user(3)));assertFalse(CanvasGroupPermissions.allows(d,user(3),"execute"));d.setGroupPermissions(Map.of("default",Map.of("read",false,"write",true,"execute",true)));assertFalse(CanvasVisibility.allows(d,user(3)));assertFalse(CanvasGroupPermissions.allows(d,user(3),"execute"));}
 @Test void policySurvivesJsonStorage()throws Exception{var d=doc();d.setGroupPermissions(Map.of("default",Map.of("read",true,"write",false,"execute",true)));var json=new com.fasterxml.jackson.databind.ObjectMapper();var restored=json.readValue(json.writeValueAsBytes(d),CanvasDocument.class);assertEquals(d.getGroupPermissions(),restored.getGroupPermissions());assertFalse(CanvasGroupPermissions.policies(restored).get("default").get("write"));}
 @Test void canonicalMembershipWinsOverStaleLegacyAliases()throws Exception{
  var mapper=new com.fasterxml.jackson.databind.ObjectMapper();var source=mapper.readTree("{\"canvas-name\":\"Aliases\",\"admin-user-id\":1,\"people\":[1,2],\"peoples\":[1],\"inner-group\":{\"admin-group\":[1],\"default\":[2]},\"group-permissions\":{\"default\":{\"read\":true,\"write\":false,\"execute\":false}}}");
  var restored=mapper.treeToValue(com.endpoint.frelog.domain.canvas.dto.CanvasDocumentJson.normalize(source),CanvasDocument.class);
  assertTrue(CanvasVisibility.allows(restored,user(2)));assertFalse(CanvasGroupPermissions.allows(restored,user(2),"write"));
  String json=mapper.writeValueAsString(restored);assertFalse(mapper.readTree(json).has("peoples"));assertFalse(mapper.readTree(json).has("canvasPassword"));
  var legacy=mapper.treeToValue(com.endpoint.frelog.domain.canvas.dto.CanvasDocumentJson.normalize(mapper.readTree("{\"peoples\":[1,2]}")),CanvasDocument.class);assertEquals(List.of(1L,2L),legacy.getPeople());
 }
 @Test void nullPolicyFailsClosedForMembers(){var d=doc();d.setGroupPermissions(null);assertFalse(CanvasVisibility.allows(d,user(2)));assertFalse(CanvasGroupPermissions.allows(d,user(2),"execute"));assertTrue(CanvasVisibility.allows(d,user(1)));}
}
