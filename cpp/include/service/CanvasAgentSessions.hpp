#pragma once
#include "service/CanvasQueryRules.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <algorithm>
#include <vector>
namespace agora::agent {
using SessionJson=nlohmann::json;
inline SessionJson sessionPermission(const SessionJson& item){
 const auto values=queryPermissionGroups(item);std::vector<std::string> groups(values.begin(),values.end());std::sort(groups.begin(),groups.end());return groups;
}
inline bool sessionVisible(const SessionJson& session,long long user,const std::unordered_set<std::string>& groups,bool admin){
 if(session.value("status",std::string())=="draft")return session.value("owner_id",0LL)==user;
 if(admin)return true;
 if(groups.empty())return false;
 const auto visibility=session.value("visibility",SessionJson::array());
 for(const auto& permission:visibility)if(!queryCanAccess(SessionJson{{"permission",permission}},groups,false))return false;
 return true;
}
inline SessionJson publicSession(SessionJson session,long long user,bool admin=false){
 session["can_delete"]=(admin||session.value("owner_id",0LL)==user)&&session.value("status",std::string())!="draft"&&session.value("status",std::string())!="running";session.erase("visibility");session.erase("running_turn_id");session["can_continue"]=session.value("owner_id",0LL)==user;return session;
}
inline std::string sessionTitle(const std::string& text){
 auto size=std::min<std::size_t>(120,text.find('\n'));size=std::min(size,text.size());
 if(size<text.size())while(size>0&&(static_cast<unsigned char>(text[size])&0xc0)==0x80)--size;
 return text.substr(0,size);
}
}
