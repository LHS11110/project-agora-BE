package com.endpoint.frelog.domain.canvas.dto;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
/** Canonical storage fields win over stale compatibility aliases, regardless of JSON order. */
public final class CanvasDocumentJson {
 private CanvasDocumentJson(){}
 private static final String[][] FIELDS={
  {"canvas-name","canvasName","canvas_name","workspace-name","workspaceName","workspace_name"},
  {"canvas-id","canvasId","canvas_id","workspace-id","workspaceId","workspace_id"},
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

 /** Serializes a normalized document with canvas-* or workspace-* primary keys for the target scope. */
 public static ObjectNode toStorageNode(ObjectMapper mapper, CanvasDocument doc, DocumentScope scope) {
  ObjectNode node = mapper.valueToTree(doc);
  if(doc.agentSessionsForStorage()!=null)node.set("agent-sessions",doc.agentSessionsForStorage().deepCopy());
  else if(doc.encodedAgentSessionsForStorage()!=null)node.set("agent-sessions-b64",doc.encodedAgentSessionsForStorage().deepCopy());
  if (scope == DocumentScope.WORKSPACE) {
   JsonNode id = node.remove("canvas-id");
   if (id != null) node.set("workspace-id", id);
   JsonNode name = node.remove("canvas-name");
   if (name != null) node.set("workspace-name", name);
  }
  return node;
 }
}
