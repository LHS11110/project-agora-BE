package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.junit.jupiter.api.Test;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasGroupBitsTest {
 @Test void bit63IsPreservedAndCapabilitiesRemainIndependent(){
  var d=new CanvasDocument();var groups=new LinkedHashMap<String,List<Long>>();var layout=new LinkedHashMap<String,Integer>();
  for(int i=0;i<64;i++){groups.put("g"+i,List.of(2L));layout.put("g"+i,i);}d.setInnerGroup(groups);d.setGroupBits(layout);
  var index=new CanvasGroupBits(d);assertTrue(index.valid);assertEquals(Long.MIN_VALUE,index.permissionMask(List.of("g63")));assertNotEquals(0,index.mask(Set.of("g63"))&index.permissionMask(List.of("g0","g63")));
  assertEquals(0,index.mask(Set.of("g62"))&index.permissionMask(List.of("g63")));
  var policies=new LinkedHashMap<String,Object>();groups.keySet().forEach(n->policies.put(n,0));policies.put("g63",5);d.setGroupPermissions(policies);
  var user=mock(CustomUserDetails.class);when(user.getUserId()).thenReturn(2L);
  assertTrue(CanvasGroupPermissions.allows(d,user,"read"));assertTrue(CanvasGroupPermissions.allows(d,user,"execute"));assertFalse(CanvasGroupPermissions.allows(d,user,"write"));
 }
 @Test void policyRoundTripUsesThreeBitIntegersAndStableIndices()throws Exception{
  var doc=new CanvasDocument("Bits",1,1L,null,"default");doc.getGroupPermissions().put("default",3);
  var mapper=new com.fasterxml.jackson.databind.ObjectMapper();var tree=mapper.valueToTree(doc);
  assertEquals(3,tree.path("group-permissions").path("default").intValue());assertEquals(0,tree.path("group-bits").path("admin-group").intValue());
  var restored=mapper.treeToValue(tree,CanvasDocument.class);assertTrue(CanvasGroupPermissions.group(restored,"default","read"));assertTrue(CanvasGroupPermissions.group(restored,"default","write"));assertFalse(CanvasGroupPermissions.group(restored,"default","execute"));
 }
 @Test void zeroAndInvalidPoliciesFailClosedAndLegacyPoliciesWork(){
  assertEquals(0,CanvasGroupBits.policyBits(0));assertEquals(0,CanvasGroupBits.policyBits(2));assertEquals(0,CanvasGroupBits.policyBits(8));assertEquals(3,CanvasGroupBits.policyBits(Map.of("read",true,"write",true,"execute",false)));
 }
}
