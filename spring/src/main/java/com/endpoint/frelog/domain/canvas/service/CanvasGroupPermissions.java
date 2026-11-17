package com.endpoint.frelog.domain.canvas.service;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.global.security.CustomUserDetails;
import java.util.*;
public final class CanvasGroupPermissions {
 private CanvasGroupPermissions(){}
 public static boolean admin(CanvasDocument doc,Long userId){return userId!=null&&(userId.equals(doc.getAdminUserId())||(doc.getInnerGroup()!=null&&doc.getInnerGroup().getOrDefault("admin-group",List.of()).contains(userId)));}
 public static boolean group(CanvasDocument doc,String name,String capability){
  if("admin-group".equals(name))return true;
  var policies=doc.getGroupPermissions();if(policies==null)return false;if(!policies.containsKey(name))return true;
  return (CanvasGroupBits.policyBits(policies.get(name))&CanvasGroupBits.capabilityBit(capability))!=0;
 }
 public static boolean allows(CanvasDocument doc,CustomUserDetails user,String capability){
  if(doc==null||user==null||user.getUserId()==null)return false;
  if(admin(doc,user.getUserId()))return true;
  // Absence preserves the behavior of existing canvases. Membership is checked by CanvasVisibility.
  if(doc.getGroupPermissions()==null)return false;
  if(doc.getGroupPermissions().isEmpty())return true;
  var index=new CanvasGroupBits(doc);
  if(index.valid){
   Set<String> members=new HashSet<>(),allowed=new HashSet<>();
   doc.getInnerGroup().forEach((name,users)->{if(users!=null&&users.contains(user.getUserId()))members.add(name);if(group(doc,name,capability))allowed.add(name);});
   if(members.isEmpty()&&"public".equals(doc.getVisibility())&&!"admin-group".equals(doc.getInitGroup())&&doc.getInnerGroup().containsKey(doc.getInitGroup()))members.add(doc.getInitGroup());
   return (index.mask(members)&index.mask(allowed))!=0;
  }
  boolean hasAssignedGroup=false;
  if(doc.getInnerGroup()!=null)for(var entry:doc.getInnerGroup().entrySet())if(entry.getValue()!=null&&entry.getValue().contains(user.getUserId())){hasAssignedGroup=true;if(group(doc,entry.getKey(),capability))return true;}
  return !hasAssignedGroup&&"public".equals(doc.getVisibility())&&!"admin-group".equals(doc.getInitGroup())&&doc.getInnerGroup()!=null&&doc.getInnerGroup().containsKey(doc.getInitGroup())&&group(doc,doc.getInitGroup(),capability);
 }
 public static Map<String,Map<String,Boolean>> policies(CanvasDocument doc){
  var result=new LinkedHashMap<String,Map<String,Boolean>>();
  if(doc.getInnerGroup()!=null)for(String name:doc.getInnerGroup().keySet())result.put(name,Map.of("read",group(doc,name,"read"),"write",group(doc,name,"write"),"execute",group(doc,name,"execute")));
  return result;
 }
}
