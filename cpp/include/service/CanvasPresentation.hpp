#pragma once
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace agora::presentation {
using Json=nlohmann::json;
inline void invalid(){throw std::invalid_argument("ITEM_PRESENTATION_INVALID");}
inline bool text(const Json& j,const char* key,std::size_t max){return j.is_object()&&j.contains(key)&&j[key].is_string()&&j[key].get_ref<const std::string&>().size()<=max;}
inline bool number(const Json& j,const char* key,double low,double high,bool integer=false){return j.contains(key)&&j[key].is_number()&&(!integer||j[key].is_number_integer())&&std::isfinite(j[key].get<double>())&&j[key].get<double>()>=low&&j[key].get<double>()<=high;}
inline void animation(const Json& a){static const std::unordered_set<std::string> effects={"none","fade","slide-up","slide-left","zoom","pulse","float","spin"},easings={"linear","ease","ease-in-out"};if(!a.is_object()||a.size()!=6||!text(a,"effect",24)||!effects.count(a["effect"].get<std::string>())||!number(a,"duration",100,30000,true)||!number(a,"delay",0,30000,true)||!number(a,"distance",0,400)||!a.contains("loop")||!a["loop"].is_boolean()||!text(a,"easing",20)||!easings.count(a["easing"].get<std::string>()))invalid();}
inline void validate(const Json& item){
 for(const auto* key:{"animationPriority","frameOrder","frameLinkPriority"})if(item.contains(key)&&!number(item,key,0,std::string(key)=="frameOrder"?1000000:9999,true))invalid();
 if(item.contains("frameId")&&(!text(item,"frameId",128)||item["frameId"]==""||item.value("kind","")=="frame"))invalid();
 if(item.value("kind","")=="frame"&&(!text(item,"title",800)||!text(item,"text",16000)||!number(item,"frameOrder",0,1000000,true)||(item.contains("rotation")&&item["rotation"]!=0)))invalid();
 if(item.contains("portfolioCursor")){const auto& c=item["portfolioCursor"];if(item.value("kind","")!="portfolio"||!c.is_object()||c.size()!=5||!text(c,"runId",128)||c["runId"]==""||!text(c,"previousRunId",128)||!text(c,"frameId",128)||c["frameId"]==""||!number(c,"step",0,10000,true)||!number(c,"startedAt",0,8640000000000000.0,true))invalid();}
if(item.contains("animation"))animation(item["animation"]);if(item.contains("animationPlayback")){const auto& p=item["animationPlayback"];if(!p.is_object()||p.size()!=4||!text(p,"runId",128)||p["runId"]==""||!text(p,"action",8)||(p["action"]!="play"&&p["action"]!="stop")||!number(p,"startedAt",0,8640000000000000.0,true)||!p.contains("config"))invalid();animation(p["config"]);}
 if(item.value("kind","")!="portfolio")return;if(!text(item,"title",800)||!text(item,"portfolioDescription",16000)||!item.contains("portfolioPages")||!item["portfolioPages"].is_array()||item["portfolioPages"].size()>100)invalid();std::unordered_set<std::string> pages;
 for(const auto& page:item["portfolioPages"]){if(!text(page,"id",128)||page["id"]==""||!pages.insert(page["id"].get<std::string>()).second||!text(page,"title",800)||page["title"]==""||!text(page,"notes",16000)||!page.contains("objectIds")||!page["objectIds"].is_array()||page["objectIds"].size()>2000)invalid();std::unordered_set<std::string> ids;for(const auto& id:page["objectIds"])if(!id.is_string()||id.get<std::string>().empty()||id.get<std::string>().size()>128||!ids.insert(id.get<std::string>()).second)invalid();}
}
inline void stamp(const Json& old,Json& item){
 if(item.contains("portfolioCursor")){auto& c=item["portfolioCursor"];const auto previous=old.is_object()?old.value("portfolioCursor",Json()):Json();if(previous.is_object()&&c.value("runId","")==previous.value("runId","")&&c!=previous)throw std::invalid_argument("PRESENTATION_STEP_STALE");if(previous.is_null()||c.value("runId","")!=previous.value("runId","")){if(c.value("previousRunId","")!=(previous.is_object()?previous.value("runId",""):""))throw std::invalid_argument("PRESENTATION_STEP_STALE");c["startedAt"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+300;}}
if(!item.contains("animationPlayback"))return;auto& p=item["animationPlayback"];if(old.is_object()&&old.value("animationPlayback",Json()).is_object()&&old["animationPlayback"].value("runId","")==p.value("runId",""))return;p["startedAt"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+300;}
}
