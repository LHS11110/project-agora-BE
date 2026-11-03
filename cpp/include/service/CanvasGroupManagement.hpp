#pragma once
#include <nlohmann/json.hpp>
#include "service/CanvasGroupBits.hpp"
#include <functional>
#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using CanvasGroupJson=nlohmann::json;
inline bool validCanvasGroupName(const std::string& name){
 if(name.empty()||name.size()>80||name=="__proto__"||name=="prototype"||name=="constructor")return false;
 if(name.front()==' '||name.back()==' ')return false;
 for(unsigned char c:name)if(c<32||c==127||c=='.')return false;
 return true;
}
inline bool canvasPermissionReferences(const CanvasGroupJson& permission,const std::string& name){
 if(permission.is_string())return permission==name;
 if(permission.is_array()){for(const auto& p:permission)if(p.is_string()&&p==name)return true;}
 return permission.is_object()&&permission.contains(name);
}
inline void renameCanvasPermission(CanvasGroupJson& permission,const std::string& before,const std::string& after){
 if(permission.is_string()&&permission==before)permission=after;
 else if(permission.is_array()){for(auto& p:permission)if(p.is_string()&&p==before)p=after;}
 else if(permission.is_object()&&permission.contains(before)){auto value=permission[before];permission.erase(before);if(!permission.contains(after))permission[after]=value;}
}
inline std::vector<std::string> applyCanvasGroupManagement(CanvasGroupJson& doc,const std::string& action,const CanvasGroupJson& value,
 const std::function<std::optional<int>(const CanvasGroupJson&)>& resolveMember){
 using J=CanvasGroupJson;
 if(action=="participant_groups"){
  if(!value.is_object()||!value.contains("groups")||!value["groups"].is_array()||value["groups"].size()>CANVAS_MAX_GROUPS||!doc.contains("inner-group")||!doc["inner-group"].is_object())throw std::invalid_argument("SETTINGS_INVALID_INPUT");
  auto id=resolveMember(value);if(!id||!doc.contains("people")||std::find(doc["people"].begin(),doc["people"].end(),J(*id))==doc["people"].end())throw std::invalid_argument("SETTINGS_USER_NOT_FOUND");
  std::set<std::string> roles;
  for(const auto& role:value["groups"]){if(!role.is_string()||!doc["inner-group"].contains(role.get<std::string>()))throw std::invalid_argument("GROUP_NOT_FOUND");roles.insert(role.get<std::string>());}
  if(*id==doc.value("admin-user-id",0)&&!roles.count("admin-group"))throw std::invalid_argument("SETTINGS_OWNER_REQUIRED");
  for(auto& [name,members]:doc["inner-group"].items()){
   if(!members.is_array())throw std::invalid_argument("SETTINGS_INVALID_INPUT");
   members.erase(std::remove(members.begin(),members.end(),J(*id)),members.end());if(roles.count(name))members.push_back(*id);
  }
  return {"inner-group"};
 }
 if(!value.is_object()||!value.contains("name")||!value["name"].is_string()||!doc.contains("inner-group")||!doc["inner-group"].is_object())throw std::invalid_argument("SETTINGS_INVALID_INPUT");
 auto name=value["name"].get<std::string>();auto& groups=doc["inner-group"];
 if(!validCanvasGroupName(name))throw std::invalid_argument("GROUP_NAME_INVALID");
 if(action=="group_create"){
  if(groups.contains(name))throw std::invalid_argument("GROUP_ALREADY_EXISTS");
  if(groups.size()>=CANVAS_MAX_GROUPS)throw std::invalid_argument("GROUP_LIMIT");
  auto policy=value.value("permissions",J{{"read",true},{"write",false},{"execute",false}});
  const auto bits=canvasPolicyInputBits(policy);if(!bits)throw std::invalid_argument("SETTINGS_INVALID_INPUT");
  groups[name]=J::array();if(!doc.contains("group-permissions")||!doc["group-permissions"].is_object())doc["group-permissions"]=J::object();
  doc["group-permissions"][name]=*bits;return {"inner-group","group-permissions"};
 }
 if(!groups.contains(name))throw std::invalid_argument("GROUP_NOT_FOUND");
 if(action=="group_members"){
  if(!value.contains("members")||!value["members"].is_array()||value["members"].size()>1000)throw std::invalid_argument("SETTINGS_INVALID_INPUT");
  std::set<int> ids;
  for(const auto& member:value["members"]){auto id=resolveMember(member);if(!id||!doc.contains("people")||std::find(doc["people"].begin(),doc["people"].end(),J(*id))==doc["people"].end())throw std::invalid_argument("SETTINGS_USER_NOT_FOUND");ids.insert(*id);}
  if(name=="admin-group"&&!ids.count(doc.value("admin-user-id",0)))throw std::invalid_argument("SETTINGS_OWNER_REQUIRED");
  groups[name]=J::array();for(int id:ids)groups[name].push_back(id);return {"inner-group"};
 }
 if(action=="group_initial"){
  if(name=="admin-group")throw std::invalid_argument("SETTINGS_OWNER_REQUIRED");
  const auto policy=doc.value("group-permissions",J::object());
  if(policy.is_object()&&policy.contains(name)&&(!(canvasPolicyBits(policy[name])&CANVAS_READ)))throw std::invalid_argument("GROUP_READ_REQUIRED");
  doc["init-group"]=name;return {"init-group"};
 }
 if(name=="admin-group")throw std::invalid_argument("SETTINGS_OWNER_REQUIRED");
 if(action=="group_delete"){
  if(doc.value("init-group",std::string("default"))==name)throw std::invalid_argument("GROUP_IS_INITIAL");
  if(doc.contains("items")&&doc["items"].is_object())for(const auto& item:doc["items"].items())if(item.value().is_object()&&item.value().contains("permission")&&canvasPermissionReferences(item.value()["permission"],name))throw std::invalid_argument("GROUP_IN_USE");
  groups.erase(name);if(doc.contains("group-bits")&&doc["group-bits"].is_object())doc["group-bits"].erase(name);if(doc.contains("group-permissions")&&doc["group-permissions"].is_object())doc["group-permissions"].erase(name);
  return {"inner-group","group-permissions"};
 }
 if(action=="group_rename"){
  if(!value.contains("new_name")||!value["new_name"].is_string())throw std::invalid_argument("GROUP_NAME_INVALID");
  auto next=value["new_name"].get<std::string>();if(!validCanvasGroupName(next))throw std::invalid_argument("GROUP_NAME_INVALID");if(groups.contains(next))throw std::invalid_argument("GROUP_ALREADY_EXISTS");
  if(doc.contains("group-bits")&&doc["group-bits"].is_object()&&doc["group-bits"].contains(name)){doc["group-bits"][next]=doc["group-bits"][name];doc["group-bits"].erase(name);}
  groups[next]=groups[name];groups.erase(name);std::vector<std::string> fields{"inner-group"};
  if(doc.contains("group-permissions")&&doc["group-permissions"].is_object()&&doc["group-permissions"].contains(name)){doc["group-permissions"][next]=doc["group-permissions"][name];doc["group-permissions"].erase(name);fields.push_back("group-permissions");}
  if(doc.value("init-group",std::string("default"))==name){doc["init-group"]=next;fields.push_back("init-group");}
  bool changed=false;
  if(doc.contains("items")&&doc["items"].is_object())for(auto& item:doc["items"].items())if(item.value().is_object()&&item.value().contains("permission")&&canvasPermissionReferences(item.value()["permission"],name)){renameCanvasPermission(item.value()["permission"],name,next);changed=true;}
  if(changed)fields.push_back("items");
  return fields;
 }
 throw std::invalid_argument("SETTINGS_INVALID_INPUT");
}
