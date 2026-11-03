package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import java.util.*;

/** Native long masks include bit 63; test overlap with != 0, never > 0. */
public final class CanvasGroupBits {
 public static final int MAX_GROUPS=64,READ=1,WRITE=2,EXECUTE=4;
 private final Map<String,Long> bits=new HashMap<>();
 private final Map<Object,Long> masks=new HashMap<>();
 public final boolean valid;
 public CanvasGroupBits(CanvasDocument doc){
  var groups=doc.getInnerGroup();valid=groups!=null&&!groups.isEmpty()&&groups.size()<=64;
  if(!valid)return;
  long used=0;var saved=doc.getGroupBits();
  for(String name:new TreeSet<>(groups.keySet()))if(saved!=null&&saved.get(name)!=null){int index=saved.get(name);if(index>=0&&index<64&&(used&(1L<<index))==0){bits.put(name,1L<<index);used|=1L<<index;}}
  for(String name:new TreeSet<>(groups.keySet()))if(!bits.containsKey(name))for(int index=0;index<64;index++)if((used&(1L<<index))==0){bits.put(name,1L<<index);used|=1L<<index;break;}
 }
 public long mask(Collection<String> groups){long result=0;if(groups!=null)for(String group:groups)result|=bits.getOrDefault(group,0L);return result;}
 public long permissionMask(Object permission){
  if(permission instanceof String name)return bits.getOrDefault(name,0L);
  if(permission==null)return 0;
  var cached=masks.get(permission);if(cached!=null)return cached;
  long result=0;
  if(permission instanceof Collection<?> groups)for(Object group:groups)if(group instanceof String name)result|=bits.getOrDefault(name,0L);
  if(permission instanceof Map<?,?> groups)for(var entry:groups.entrySet())if(Boolean.TRUE.equals(entry.getValue())||(entry.getValue() instanceof Number n&&n.doubleValue()>0))result|=bits.getOrDefault(entry.getKey(),0L);
  if(masks.size()>=2048)masks.clear();masks.put(permission,result);return result;
 }
 public static int policyBits(Object policy){
  if(policy instanceof Number n){double d=n.doubleValue();int value=n.intValue();return d==value&&value>=0&&value<=7&&(value&READ)!=0?value:0;}
  if(!(policy instanceof Map<?,?> p)||!Boolean.TRUE.equals(p.get("read")))return 0;
  return READ|(Boolean.TRUE.equals(p.get("write"))?WRITE:0)|(Boolean.TRUE.equals(p.get("execute"))?EXECUTE:0);
 }
 public static int capabilityBit(String name){return switch(name){case "read"->READ;case "write"->WRITE;case "execute"->EXECUTE;default->0;};}
}
