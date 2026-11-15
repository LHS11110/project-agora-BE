#pragma once
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <unordered_set>
#include <stdexcept>
#include <cmath>
namespace agora::agent {
using Json=nlohmann::json;
inline void options(const Json& value){
 if(!value.is_object()||value.size()>5)throw std::invalid_argument("AGENT_OPTIONS_INVALID");
 for(const auto& entry:value.items()){
  const auto name=entry.key();const auto& v=entry.value();
  if(name=="reasoning_effort"){if(!v.is_string()||(v!="low"&&v!="high"&&v!="max"))throw std::invalid_argument("AGENT_OPTIONS_INVALID");continue;}
  double low=0,high=0;
  if(name=="max_tokens"){low=512;high=32768;}else if(name=="max_steps"){low=1;high=12;}
  else if(name=="temperature"){low=0;high=2;}else if(name=="top_p"){low=.05;high=1;}
  else throw std::invalid_argument("AGENT_OPTIONS_INVALID");
  if(!v.is_number()||!std::isfinite(v.get<double>())||v.get<double>()<low||v.get<double>()>high||((name=="max_tokens"||name=="max_steps")&&!v.is_number_integer()))throw std::invalid_argument("AGENT_OPTIONS_INVALID");
 }
}
inline std::string randomId(){unsigned char bytes[16];if(RAND_bytes(bytes,sizeof(bytes))!=1)throw std::runtime_error("AGENT_UNAVAILABLE");const char* hex="0123456789abcdef";std::string id="agent-";for(auto b:bytes){id+=hex[b>>4];id+=hex[b&15];}return id;}
inline void fields(const Json& item){
 static const std::unordered_set<std::string> allowed={"kind","text","code","formula","title","filename","language","renderMode","x","y","width","height","depth","rotation","color","fill","strokeWidth","shapeType","url","src","mediaType","videoId","embedUrl","from","to","fromPort","toPort","startHead","endHead","label","labelMode"};
 if(!item.is_object()||item.empty()||item.dump().size()>100000)throw std::invalid_argument("AGENT_ITEM_INVALID");
 for(const auto& entry:item.items())if(!allowed.count(entry.key()))throw std::invalid_argument("AGENT_FIELD_DENIED");
 if(item.contains("kind")&&!std::unordered_set<std::string>{"text","code","math","shape","note","connector","link","image"}.count(item["kind"].get<std::string>()))throw std::invalid_argument("AGENT_KIND_DENIED");
}
inline Json publicItem(const Json& item,bool source){Json result=Json::object();for(const auto* key:{"kind","runtimeRole","title","filename","language","renderMode","x","y","width","height","depth","color","fill","shapeType","locked","from","to","url"})if(item.contains(key))result[key]=item[key];
 if(!source)for(const auto* key:{"title","filename","url"})if(result.contains(key)&&result[key].is_string()&&result[key].get_ref<const std::string&>().size()>512)result.erase(key);
 if(source)for(const auto* key:{"text","code","formula"})if(item.contains(key))result[key]=item[key];
 return result;
}
}
