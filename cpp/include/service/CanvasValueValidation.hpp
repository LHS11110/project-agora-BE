#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>
namespace agora::value {
inline void validate(const nlohmann::json& item){
 if(item.value("kind","")!="value")return;
 auto invalid=[](){throw std::invalid_argument("ITEM_VALUE_INVALID");};
 if(!item.contains("valueType")||!item["valueType"].is_string())invalid();
 const auto type=item["valueType"].get<std::string>();
 if(type!="real"&&type!="integer"&&type!="button"&&type!="select"&&type!="text")invalid();
 for(const auto* field:{"valueMin","valueMax","valueStep","valueNumber"})if(!item.contains(field)||!item[field].is_number()||!std::isfinite(item[field].get<double>()))invalid();
 const double min=item["valueMin"],max=item["valueMax"],step=item["valueStep"],number=item["valueNumber"];
 if(std::abs(min)>1e12||std::abs(max)>1e12||min>=max||step<=0||step>max-min)invalid();
 if(!item.contains("title")||!item["title"].is_string()||item["title"].get_ref<const std::string&>().size()>512)invalid();
 if(!item.contains("valueText")||!item["valueText"].is_string()||item["valueText"].get_ref<const std::string&>().size()>16384)invalid();
 if(!item.contains("valueOptions")||!item["valueOptions"].is_array()||item["valueOptions"].empty()||item["valueOptions"].size()>32)invalid();
 for(const auto& option:item["valueOptions"])if(!option.is_string()||option.get_ref<const std::string&>().empty()||option.get_ref<const std::string&>().size()>512)invalid();
 if(type=="real"&&(number<min||number>max))invalid();
 if(type=="integer"&&(std::floor(min)!=min||std::floor(max)!=max||std::floor(step)!=step||std::floor(number)!=number||number<min||number>max))invalid();
 if(type=="button"&&number!=0&&number!=1)invalid();
 if(type=="select"&&(std::floor(number)!=number||number<0||number>=item["valueOptions"].size()))invalid();
}
}
namespace agora::shared {
inline bool runtimeTarget(const nlohmann::json& item){return item.value("kind","")=="code"||(item.value("kind","")=="text"&&item.value("renderMode","")=="code")||(item.value("kind","")=="shape"&&(item.value("runtimeRole","")=="executor"||item.value("runtimeRole","")=="notebook"));}
inline void validate(const nlohmann::json& item){
 if(item.contains("mediaState")){
  const auto& state=item["mediaState"];
  if(item.value("kind","")!="link"||!state.is_object()||state.size()!=4||!state.contains("paused")||!state["paused"].is_boolean()||!state.contains("time")||!state["time"].is_number()||!std::isfinite(state["time"].get<double>())||state["time"].get<double>()<0||state["time"].get<double>()>31536000||!state.contains("updatedAt")||!state["updatedAt"].is_number_integer()||state["updatedAt"].get<long long>()<0||!state.contains("actor")||!state["actor"].is_string()||state["actor"].get_ref<const std::string&>().size()>128)throw std::invalid_argument("ITEM_MEDIA_STATE_INVALID");
 }
 if(item.contains("runtimeState")){
  const auto& state=item["runtimeState"];
  if(!runtimeTarget(item))throw std::invalid_argument("ITEM_RUNTIME_STATE_INVALID");
  if(state.is_null())return;
  if(!state.is_object()||state.dump().size()>262144||!state.contains("updatedAt")||!state["updatedAt"].is_number_integer()||state["updatedAt"].get<long long>()<0)throw std::invalid_argument("ITEM_RUNTIME_STATE_INVALID");
  static const std::unordered_set<std::string> booleans={"running","building","completed","failed","built","reset"},strings={"status","phase","runId","source","language","detail","error"},statuses={"running","queued","success","error","skipped"};
  for(const auto& field:state.items()){
   if(field.key()=="updatedAt")continue;
   if(booleans.count(field.key())){if(!field.value().is_boolean())throw std::invalid_argument("ITEM_RUNTIME_STATE_INVALID");}
   else if(strings.count(field.key())){if(!field.value().is_string())throw std::invalid_argument("ITEM_RUNTIME_STATE_INVALID");}
   else throw std::invalid_argument("ITEM_RUNTIME_STATE_INVALID");
  }
  if(state.contains("status")&&!statuses.count(state["status"].get<std::string>()))throw std::invalid_argument("ITEM_RUNTIME_STATE_INVALID");
 }
}
}
