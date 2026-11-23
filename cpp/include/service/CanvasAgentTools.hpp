#pragma once
#include "service/CanvasBrokerOperations.hpp"
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <algorithm>
#include <unordered_set>
#include <stdexcept>
#include <cmath>
#include <cctype>
namespace agora::agent {
using Json=nlohmann::json;
inline void options(const Json& value){
 if(!value.is_object()||value.size()>9)throw std::invalid_argument("AGENT_OPTIONS_INVALID");
 for(const auto& entry:value.items()){
  const auto name=entry.key();const auto& v=entry.value();
  if(name=="reasoning_effort"){if(!v.is_string()||(v!="low"&&v!="high"&&v!="max"))throw std::invalid_argument("AGENT_OPTIONS_INVALID");continue;}
  // Agents run through the API engine only; VM-hosted CLI agents were removed.
  if(name=="engine"){if(!v.is_string()||v!="api")throw std::invalid_argument("AGENT_OPTIONS_INVALID");continue;}
  // Providers and their endpoints are fixed by the agent service; only an id is accepted.
  // EXAONE is excluded: its API terms forbid integrating the API into products.
  if(name=="provider"){static const std::unordered_set<std::string> providers={"openai","anthropic","gemini","openrouter","local"};if(!v.is_string()||!providers.count(v.get<std::string>()))throw std::invalid_argument("AGENT_OPTIONS_INVALID");continue;}
  // Empty selects the provider default; otherwise [A-Za-z0-9][A-Za-z0-9._:/@-]{0,127}.
  if(name=="model"){if(!v.is_string())throw std::invalid_argument("AGENT_OPTIONS_INVALID");const auto& model=v.get_ref<const std::string&>();
   if(model.size()>128||(!model.empty()&&!std::isalnum(static_cast<unsigned char>(model[0]))))throw std::invalid_argument("AGENT_OPTIONS_INVALID");
   for(unsigned char c:model)if(!std::isalnum(c)&&c!='.'&&c!='_'&&c!=':'&&c!='/'&&c!='@'&&c!='-')throw std::invalid_argument("AGENT_OPTIONS_INVALID");continue;}
  if(name=="locale"){if(!v.is_string()||(v!="ko"&&v!="en"&&v!="zh"&&v!="ja"&&v!="fr"))throw std::invalid_argument("AGENT_OPTIONS_INVALID");continue;}
  double low=0,high=0;
  if(name=="max_tokens"){low=512;high=32768;}else if(name=="max_steps"){low=1;high=12;}
  else if(name=="temperature"){low=0;high=2;}else if(name=="top_p"){low=.05;high=1;}
  else throw std::invalid_argument("AGENT_OPTIONS_INVALID");
  if(!v.is_number()||!std::isfinite(v.get<double>())||v.get<double>()<low||v.get<double>()>high||((name=="max_tokens"||name=="max_steps")&&!v.is_number_integer()))throw std::invalid_argument("AGENT_OPTIONS_INVALID");
 }
}
inline std::string randomId(){unsigned char bytes[16];if(RAND_bytes(bytes,sizeof(bytes))!=1)throw std::runtime_error("AGENT_UNAVAILABLE");const char* hex="0123456789abcdef";std::string id="agent-";for(auto b:bytes){id+=hex[b>>4];id+=hex[b&15];}return id;}
inline std::string confirmedDeleteId(const Json& event,const Json& selected,const std::string& operation){
 const auto target=event.value("confirmed_delete_id",std::string());
 if(!target.empty()&&(operation!="run"||selected.size()!=1||selected[0]!=target))
  throw std::invalid_argument("AGENT_DELETE_DISABLED");
 return target;
}
inline bool matchesConfirmedDelete(const Json& confirmed,const Json& args){
 return confirmed.is_object()&&confirmed.contains("revision")&&
  args==Json{{"objects",Json::array({confirmed})}};
}
inline std::string confirmedEditId(const Json& event,const Json& selected,const std::string& operation){
 const auto target=event.value("confirmed_edit_id",std::string());
 if(!target.empty()&&(operation!="run"||selected.size()!=1||selected[0]!=target||
                     !event.value("confirmed_delete_id",std::string()).empty()))
  throw std::invalid_argument("AGENT_EDIT_SCOPE_DENIED");
 return target;
}
inline void requireMutationScope(bool public_canvas,const std::string& tool,const Json& args,const std::string& edit_id){
 if(public_canvas&&tool!="patch_objects")throw std::invalid_argument("AGENT_PUBLIC_PUBLISH_DENIED");
 if(edit_id.empty()){
  if(public_canvas)throw std::invalid_argument("AGENT_PUBLIC_EDIT_APPROVAL_REQUIRED");
  return;
 }
 const auto changes=args.value("changes",Json::array());
 if(tool!="patch_objects"||!changes.is_array()||changes.size()!=1||!changes[0].is_object()||
    changes[0].value("id",std::string())!=edit_id)
  throw std::invalid_argument("AGENT_EDIT_SCOPE_DENIED");
}
inline bool manageableField(const std::string& field){
 static const std::unordered_set<std::string> protected_fields={
  "permission","locked","aiGenerated","members","groupId","automerge_snapshot","automerge_changes",
  "runtimeState","runtimeResults","notebookResult","notebookResults","projectHistory",
  "questionAnswer","questionScore","questionFeedback","questionSubmitted","questionGradedVersion"
 };
 return agora::broker::objectFields().count(field)&&!protected_fields.count(field);
}
inline void fields(const Json& item){
 if(!item.is_object()||item.empty()||item.dump().size()>100000)throw std::invalid_argument("AGENT_ITEM_INVALID");
 for(const auto& entry:item.items())if(!manageableField(entry.key()))throw std::invalid_argument("AGENT_FIELD_DENIED");
}
// Keep normalized wire coordinates and use the same world-pixel grid as the FE.
inline void snapGeometry(Json& patch) {
 struct Field { const char* name; double pixels; bool size; };
 constexpr Field fields[] = {{"x", 1600, false}, {"y", 1000, false},
                             {"width", 1600, true}, {"height", 1000, true}};
 for (const auto& field : fields) {
  auto value = patch.find(field.name);
  if (value == patch.end()) continue;
  if (!value->is_number()) throw std::invalid_argument("ITEM_NUMBER_INVALID");
  const auto pixels = value->get<double>() * field.pixels;
  if (!std::isfinite(pixels)) throw std::invalid_argument("ITEM_NUMBER_INVALID");
  if (field.size && value->get<double>() <= 0) throw std::invalid_argument("ITEM_SIZE_INVALID");
  const auto lower = std::floor(pixels);
  // Match Math.round: half-pixel ties go toward positive infinity.
  const auto rounded = lower + (pixels - lower >= 0.5 ? 1 : 0);
  *value = (field.size ? std::max(1.0, rounded) : rounded) / field.pixels;
 }
}
inline Json publicItem(const Json& item,bool source){
 Json result=Json::object();
 if(source){
  for(const auto& field:item.items())if(manageableField(field.key()))result[field.key()]=field.value();
 }else{
  for(const auto* key:{"kind","runtimeRole","title","name","filename","language","renderMode","x","y","from","to"}){
   if(item.contains(key)&&item[key].dump().size()<=512)result[key]=item[key];
  }
  if(item.contains("text")&&item["text"].is_string()&&item["text"].dump().size()<=240)result["text"]=item["text"];
 }
 for(const auto* key:{"locked","aiGenerated"})if(item.contains(key))result[key]=item[key];
 return result;
}
}
