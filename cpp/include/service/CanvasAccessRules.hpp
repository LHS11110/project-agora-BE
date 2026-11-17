#pragma once
#include <nlohmann/json.hpp>
#include "service/CanvasGroupBits.hpp"
#include <string>
#include <unordered_set>
#include <utility>

inline bool canvasIsPublic(const nlohmann::json& doc) {
    return doc.is_object() && doc.contains("visibility") && doc["visibility"] == "public";
}

inline bool canvasHasMember(const nlohmann::json& doc, int user_id) {
    if (!doc.is_object() || !doc.contains("people") || !doc["people"].is_array()) return false;
    for (const auto& member : doc["people"]) if (member.is_number_integer() && member == user_id) return true;
    return false;
}

inline bool canvasGroupCapability(const nlohmann::json& doc,const std::string& group,const std::string& capability) {
    return (canvasStoredGroupBits(doc,group)&canvasCapabilityBit(capability))!=0;
}
inline nlohmann::json canvasGroupPolicies(const nlohmann::json& doc) {
    nlohmann::json result=nlohmann::json::object();
    if(doc.contains("inner-group")&&doc["inner-group"].is_object())for(const auto& entry:doc["inner-group"].items())
        result[entry.key()]={{"read",canvasGroupCapability(doc,entry.key(),"read")},{"write",canvasGroupCapability(doc,entry.key(),"write")},{"execute",canvasGroupCapability(doc,entry.key(),"execute")}};
    return result;
}
inline std::pair<std::unordered_set<std::string>, bool> groupsForUser(const nlohmann::json& doc, int user_id, const std::string& capability="read") {
    std::unordered_set<std::string> groups;
    bool is_admin = false;
    bool has_assigned_group = false;
    if (!doc.contains("inner-group") || !doc["inner-group"].is_object()) return {groups, false};
    for (const auto& [group, members] : doc["inner-group"].items()) {
        if (!members.is_array()) continue;
        for (const auto& member : members) {
            if (member.is_number_integer() && member.get<int>() == user_id) {
                has_assigned_group = true;
                if(canvasGroupCapability(doc,group,capability))groups.insert(group);
                if (group == "admin-group") is_admin = true;
                break;
            }
        }
    }
    if (!has_assigned_group && canvasIsPublic(doc) && doc.contains("init-group") && doc["init-group"].is_string()) {
        auto group = doc["init-group"].get<std::string>();
        if (!group.empty() && group != "admin-group" && doc["inner-group"].contains(group) && canvasGroupCapability(doc,group,capability)) groups.insert(group);
    }
    return {std::move(groups), is_admin};
}


// Membership is distinct from read/write capabilities: an object can be shared
// with a read-only role without granting that role editing rights.
inline std::unordered_set<std::string> canvasMemberGroups(const nlohmann::json& doc,int user_id){
 std::unordered_set<std::string> result;
 if(doc.contains("inner-group")&&doc["inner-group"].is_object())for(const auto& [name,members]:doc["inner-group"].items())
  if(members.is_array())for(const auto& member:members)if(member.is_number_integer()&&member==user_id){result.insert(name);break;}
 if(result.empty() && canvasIsPublic(doc)&&doc.contains("init-group")&&doc["init-group"].is_string()){
  const auto name=doc["init-group"].get<std::string>();
  if(name!="admin-group"&&doc.contains("inner-group")&&doc["inner-group"].contains(name))result.insert(name);
 }
 return result;
}
