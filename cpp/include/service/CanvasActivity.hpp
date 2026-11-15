#pragma once
#include <nlohmann/json.hpp>
#include <ctime>
#include <chrono>
namespace agora::activity {
using Json=nlohmann::json;
// Wall-clock half-hour boundaries keep deployments from postponing persistence.
inline std::chrono::system_clock::time_point nextFlushTime(std::chrono::system_clock::time_point now){
 const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
 return std::chrono::system_clock::time_point{std::chrono::seconds{(seconds/1800+1)*1800}};
}
inline std::string koreanDay(){std::time_t now=std::time(nullptr)+9*3600;std::tm tm{};gmtime_r(&now,&tm);char day[11];std::strftime(day,sizeof(day),"%Y-%m-%d",&tm);return day;}
inline Json visible(Json item){if(!item.is_object())return item;for(auto it=item.begin();it!=item.end();){const auto k=it.key();if(k.rfind("__",0)==0||k=="runtimeState"||k=="runtimeResults"||k=="notebookResults"||k=="notebookResult"||k=="mediaState"||k=="mediaLoop"||k=="animationPlayback"||k=="portfolioCursor")it=item.erase(it);else ++it;}return item;}
inline Json delta(const Json& previous,const Json& changed){long long created=0,updated=0,deleted=0;for(const auto& entry:changed.items()){
 const auto old=previous.value(entry.key(),Json());const auto item=entry.value();const auto kind=item.is_null()?old.value("kind",""):item.value("kind","");
 if(kind=="portfolio"||kind=="host-tombstone")continue;
 if(kind=="stroke"&&!item.is_null()&&!item.value("complete",true))continue;
 if(item.is_null()){if(!old.is_null()&&(kind!="stroke"||old.value("complete",true)))++deleted;}
 else if(old.is_null()||(kind=="stroke"&&!old.value("complete",true)))++created;
 else if(visible(old)!=visible(item))++updated;
 }return {{"day",koreanDay()},{"created",created},{"updated",updated},{"deleted",deleted}};}
}
