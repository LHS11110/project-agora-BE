#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline constexpr std::size_t CANVAS_MAX_GROUPS=64;
inline constexpr std::uint8_t CANVAS_READ=1,CANVAS_WRITE=2,CANVAS_EXECUTE=4;
inline std::uint8_t canvasCapabilityBit(const std::string& name){return name=="read"?CANVAS_READ:name=="write"?CANVAS_WRITE:name=="execute"?CANVAS_EXECUTE:0;}
inline std::uint8_t canvasPolicyBits(const nlohmann::json& value){
 if(value.is_number_integer()){auto n=value.get<long long>();return n>=0&&n<=7&&(n&1)?static_cast<std::uint8_t>(n):0;}
 if(!value.is_object()||!value.contains("read")||value["read"]!=true)return 0;
 return CANVAS_READ|(value.contains("write")&&value["write"]==true?CANVAS_WRITE:0)|(value.contains("execute")&&value["execute"]==true?CANVAS_EXECUTE:0);
}
inline std::optional<std::uint8_t> canvasPolicyInputBits(const nlohmann::json& value){
 if(value.is_number_integer()){auto n=value.get<long long>();if(n<0||n>7||((n&6)&&!(n&1)))return std::nullopt;return static_cast<std::uint8_t>(n);}
 if(!value.is_object()||value.size()!=3)return std::nullopt;
 for(const auto* name:{"read","write","execute"})if(!value.contains(name)||!value[name].is_boolean())return std::nullopt;
 if((value["write"]==true||value["execute"]==true)&&value["read"]!=true)return std::nullopt;
 return canvasPolicyBits(value);
}
inline std::uint8_t canvasStoredGroupBits(const nlohmann::json& doc,const std::string& name){
 if(name=="admin-group")return 7;
 if(!doc.contains("group-permissions"))return 7;
 const auto& policies=doc["group-permissions"];if(!policies.is_object())return 0;
 return policies.contains(name)?canvasPolicyBits(policies[name]):7;
}
inline std::string canvasMaskHex(std::uint64_t mask){std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<mask;return out.str();}
inline nlohmann::json canvasGroupBitLayout(const nlohmann::json& doc){
 using J=nlohmann::json;J result=J::object();std::uint64_t used=0;
 if(!doc.contains("inner-group")||!doc["inner-group"].is_object()||doc["inner-group"].size()>CANVAS_MAX_GROUPS)return result;
 const auto saved=doc.value("group-bits",J::object());
 for(const auto& entry:doc["inner-group"].items())if(saved.is_object()&&saved.contains(entry.key())&&saved[entry.key()].is_number_integer()){
  auto index=saved[entry.key()].get<long long>();if(index>=0&&index<64&&!(used&(std::uint64_t{1}<<index))){result[entry.key()]=index;used|=std::uint64_t{1}<<index;}
 }
 for(const auto& entry:doc["inner-group"].items())if(!result.contains(entry.key()))for(unsigned index=0;index<64;++index)if(!(used&(std::uint64_t{1}<<index))){result[entry.key()]=index;used|=std::uint64_t{1}<<index;break;}
 return result;
}
inline nlohmann::json canvasPackedPolicies(const nlohmann::json& doc){
 nlohmann::json result=nlohmann::json::object();if(doc.contains("inner-group")&&doc["inner-group"].is_object())for(const auto& entry:doc["inner-group"].items())result[entry.key()]=canvasStoredGroupBits(doc,entry.key());return result;
}
class CanvasGroupBitIndex {
 std::unordered_map<std::string,std::uint64_t> bits_;
 mutable std::unordered_map<std::string,std::uint64_t> acl_cache_;
 public:
 bool valid=false;
 explicit CanvasGroupBitIndex(const nlohmann::json& layout){
  if(!layout.is_object()||layout.empty()||layout.size()>64)return;
  std::uint64_t used=0;
  for(const auto& entry:layout.items()){
   if(!entry.value().is_number_integer()){bits_.clear();return;}
   auto i=entry.value().get<long long>();if(i<0||i>=64||(used&(std::uint64_t{1}<<i))){bits_.clear();return;}
   const auto bit=std::uint64_t{1}<<i;bits_[entry.key()]=bit;used|=bit;
  }valid=true;
 }
 std::uint64_t bit(const std::string& name)const{auto it=bits_.find(name);return it==bits_.end()?0:it->second;}
 std::uint64_t mask(const std::unordered_set<std::string>& groups)const{std::uint64_t result=0;for(const auto& name:groups)result|=bit(name);return result;}
 std::uint64_t mask(const nlohmann::json& permission)const{
  if(permission.is_string())return bit(permission.get_ref<const std::string&>());
  const auto key=permission.dump();auto cached=acl_cache_.find(key);if(cached!=acl_cache_.end())return cached->second;
  std::uint64_t result=0;
  if(permission.is_array())for(const auto& name:permission)if(name.is_string())result|=bit(name.get_ref<const std::string&>());
  if(permission.is_object())for(const auto& entry:permission.items())if(entry.value()==true||(entry.value().is_number()&&entry.value().get<double>()>0))result|=bit(entry.key());
  if(acl_cache_.size()>=2048)acl_cache_.clear();
  acl_cache_[key]=result;return result;
 }
};
// Request-local binding avoids rebuilding a user's role mask for each object.
// RAII also restores an outer scope, and thread-local storage isolates workers.
class CanvasAclScope;
inline thread_local const CanvasAclScope* currentCanvasAcl=nullptr;
class CanvasAclScope {
 const CanvasAclScope* previous_;
 std::vector<std::pair<const std::unordered_set<std::string>*,std::uint64_t>> masks_;
 public:
 CanvasGroupBitIndex index;
 explicit CanvasAclScope(const nlohmann::json& layout,std::initializer_list<const std::unordered_set<std::string>*> groups):previous_(currentCanvasAcl),index(layout){
  for(auto value:groups)masks_.push_back({value,index.mask(*value)});
  currentCanvasAcl=index.valid?this:nullptr;
 }
 ~CanvasAclScope(){currentCanvasAcl=previous_;}
 CanvasAclScope(const CanvasAclScope&)=delete;CanvasAclScope& operator=(const CanvasAclScope&)=delete;
 std::uint64_t userMask(const std::unordered_set<std::string>& groups)const{for(const auto& entry:masks_)if(entry.first==&groups)return entry.second;return index.mask(groups);}
 bool allows(const nlohmann::json& permission,const std::unordered_set<std::string>& groups)const{return (index.mask(permission)&userMask(groups))!=0;}
};
