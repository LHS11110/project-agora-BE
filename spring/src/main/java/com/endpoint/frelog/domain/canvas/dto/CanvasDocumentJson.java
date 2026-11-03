package com.endpoint.frelog.domain.canvas.dto;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
/** Canonical storage fields win over stale compatibility aliases, regardless of JSON order. */
public final class CanvasDocumentJson {
 private CanvasDocumentJson(){}
 private static final String[][] FIELDS={
  {"canvas-name","canvasName","canvas_name"},{"canvas-id","canvasId","canvas_id"},
  {"admin-user-id","admin","adminUserId","admin_user_id"},{"people","peoples"},
  {"inner-group","innerGroup","inner_group"},{"init-group","initGroup","init_group"},
  {"canvas-password-hash","canvasPassword","canvas_password_hash","canvas-password"},
  {"group-permissions","group_permissions"},{"group-bits","group_bits"}
 };
 public static ObjectNode normalize(JsonNode source){
  if(source==null||!source.isObject())throw new IllegalArgumentException("Invalid canvas document");
  ObjectNode result=((ObjectNode)source).deepCopy();
  for(var names:FIELDS){if(!result.has(names[0]))for(int i=1;i<names.length;i++)if(result.has(names[i])){result.set(names[0],result.get(names[i]));break;}for(int i=1;i<names.length;i++)result.remove(names[i]);}
  return result;
 }
}
