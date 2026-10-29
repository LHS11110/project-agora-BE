#pragma once
#include "service/CanvasQueryRules.hpp"
#include <functional>
#include <map>
#include <cmath>
#include <optional>
namespace agora::broker {
using Json=nlohmann::json;
struct Mutation {Json items=Json::object(),previous=Json::object(),expected=Json::object(),translations=Json::array();bool only_translations=true;};
inline void validate(const Json& item){
 static const std::unordered_set<std::string> kinds={"text","note","code","math","shape","table","chart","image","link","connector","stroke","pdf","user-group"};
 static const std::unordered_set<std::string> fields={"kind","text","code","formula","format","filename","language","shapeType","visualStyle","volumeStyle","chartType","color","fill","permission","groupId","src","url","title","mediaType","from","to","fromPort","toPort","startHead","endHead","brush","embedUrl","videoId","renderMode","label","labelMode","groupTitle","thumbnail","cornerStyle","runtimeRole","runtimeResults","shadowBlur","shadowOpacity","shadowOffsetX","shadowOffsetY","depth","x","y","width","height","rotation","fontSize","strokeWidth","bend","opacity","contentScale","contentScaleX","contentScaleY","bendPointCount","headSize","vertexCount","fillOpacity","sketchFillOpacity","points","rows","columns","simulatePressure","complete","strokeEnabled","shadowEnabled","imageFrameEnabled","bendPoints","members","automerge_snapshot","automerge_changes"};
 if(!item.is_object()||!kinds.count(item.value("kind","")))throw std::invalid_argument("ITEM_KIND_INVALID");
 for(const auto& field:item.items()){
  if(!fields.count(field.key()))throw std::invalid_argument("ITEM_FIELD_INVALID");
  if(field.value().is_number()&&!std::isfinite(field.value().get<double>()))throw std::invalid_argument("ITEM_NUMBER_INVALID");
 }
 for(const auto* field:{"x","y","width","height","rotation","fontSize","strokeWidth","depth","shadowBlur","shadowOpacity","shadowOffsetX","shadowOffsetY","bend","opacity","contentScale","contentScaleX","contentScaleY","bendPointCount","headSize","vertexCount","fillOpacity","sketchFillOpacity"})if(item.contains(field)&&!item[field].is_number())throw std::invalid_argument("ITEM_NUMBER_INVALID");
 for(const auto* field:{"width","height","fontSize","strokeWidth"})if(item.contains(field)&&item[field].get<double>()<=0)throw std::invalid_argument("ITEM_SIZE_INVALID");
 if(item.contains("volumeStyle")&&(!item["volumeStyle"].is_string()||(item["volumeStyle"]!="solid"&&item["volumeStyle"]!="wireframe")))throw std::invalid_argument("ITEM_VOLUME_STYLE_INVALID");
 if(item.contains("runtimeRole")&&(item.value("kind","")!="shape"||!item["runtimeRole"].is_string()||(item["runtimeRole"]!="executor"&&item["runtimeRole"]!="output")))throw std::invalid_argument("ITEM_RUNTIME_ROLE_INVALID");
 if(item.contains("runtimeResults")) {
  const auto& results=item["runtimeResults"];
  if(item.value("runtimeRole","")!="output"||!results.is_array()||results.size()>32||results.dump().size()>524288)throw std::invalid_argument("ITEM_RUNTIME_RESULT_INVALID");
  static const std::unordered_set<std::string> result_fields={"runId","codeId","createdAt","output","error","exitCode","timedOut","truncated"};
  for(const auto& entry:results){
   if(!entry.is_object()||!entry.contains("runId")||!entry["runId"].is_string()||entry["runId"].get<std::string>().size()>256||!entry.contains("codeId")||!entry["codeId"].is_string()||entry["codeId"].get<std::string>().size()>128||!entry.contains("createdAt")||!entry["createdAt"].is_number_integer()||entry["createdAt"].get<long long>()<0)throw std::invalid_argument("ITEM_RUNTIME_RESULT_INVALID");
   for(const auto& field:entry.items())if(!result_fields.count(field.key()))throw std::invalid_argument("ITEM_RUNTIME_RESULT_INVALID");
   for(const auto* field:{"output","error"})if(entry.contains(field)&&!entry[field].is_string())throw std::invalid_argument("ITEM_RUNTIME_RESULT_INVALID");
   if(entry.contains("exitCode")&&!entry["exitCode"].is_number_integer())throw std::invalid_argument("ITEM_RUNTIME_RESULT_INVALID");
   for(const auto* field:{"timedOut","truncated"})if(entry.contains(field)&&!entry[field].is_boolean())throw std::invalid_argument("ITEM_RUNTIME_RESULT_INVALID");
  }
 }
 if(item.value("kind","")=="chart"){
  static const std::unordered_set<std::string> chart_types={"bar","line","area","pie","donut","scatter"};
  if(!item.contains("chartType")||!item["chartType"].is_string()||!chart_types.count(item["chartType"].get<std::string>()))throw std::invalid_argument("ITEM_CHART_TYPE_INVALID");
  if(item.contains("title")&&(!item["title"].is_string()||item["title"].get<std::string>().size()>480))throw std::invalid_argument("ITEM_CHART_DATA_INVALID");
  if(!item.contains("columns")||!item["columns"].is_array()||item["columns"].size()<2||item["columns"].size()>12||!item.contains("rows")||!item["rows"].is_array()||item["rows"].empty()||item["rows"].size()>100)throw std::invalid_argument("ITEM_CHART_DATA_INVALID");
  for(const auto& cell:item["columns"])if(!cell.is_string()||cell.get<std::string>().size()>4000)throw std::invalid_argument("ITEM_CHART_DATA_INVALID");
  for(const auto& row:item["rows"]){if(!row.is_array()||row.size()!=item["columns"].size())throw std::invalid_argument("ITEM_CHART_DATA_INVALID");for(const auto& cell:row)if(!cell.is_string()||cell.get<std::string>().size()>4000)throw std::invalid_argument("ITEM_CHART_DATA_INVALID");}
  if(item["columns"].dump().size()+item["rows"].dump().size()>300000)throw std::invalid_argument("ITEM_CHART_DATA_INVALID");
 }
 if(item.contains("bendPoints")){
  const auto& points=item["bendPoints"];const int count=item.value("bendPointCount",1);
  if(!points.is_array()||count<1||count>32||points.size()!=static_cast<std::size_t>(count))throw std::invalid_argument("ITEM_BEND_INVALID");
  for(const auto& point:points)if(!point.is_object()||!point.contains("x")||!point["x"].is_number()||!point.contains("y")||!point["y"].is_number())throw std::invalid_argument("ITEM_BEND_INVALID");
 }
 if(item.value("kind","")=="stroke"){
  if(!item.contains("points")||!item["points"].is_array()||item["points"].size()>100000)throw std::invalid_argument("ITEM_STROKE_INVALID");
  for(const auto& point:item["points"])if(!point.is_object()||!point.contains("x")||!point["x"].is_number()||!point.contains("y")||!point["y"].is_number())throw std::invalid_argument("ITEM_STROKE_INVALID");
 }
}
inline Mutation prepare(const Json& operations,const std::function<std::optional<Json>(const std::string&)>& read,
 const std::unordered_set<std::string>& groups,bool admin){
 if(!operations.is_array()||operations.empty()||operations.size()>512)throw std::invalid_argument("QUERY_BATCH_INVALID");
 Mutation result;
 auto get=[&](const std::string& id)->Json{
  if(!validQueryItemId(id))throw std::invalid_argument("ITEM_ID_INVALID");
  if(result.items.contains(id))return result.items[id];
  const auto loaded=read(id);Json old=loaded.value_or(Json());
  if(!old.is_null()&&!queryCanAccess(old,groups,admin))throw std::invalid_argument("ITEM_ACCESS_DENIED");
  result.previous[id]=old;result.expected[id]=old.is_null()?-1:old.value("__broker_revision",0LL);
  if(!old.is_null()&&old.value("kind","")=="host-tombstone")old=nullptr;
  if(!old.is_null()){old.erase("__host_version");old.erase("__broker_revision");}
  result.items[id]=old;return old;
 };
 for(const auto& operation:operations){
  if(!operation.is_object())throw std::invalid_argument("QUERY_ACTION_INVALID");
  const auto action=operation.value("action","");
  if(action=="translate"){
   const auto ids=operation.value("ids",Json::array());
   if(!ids.is_array()||ids.empty()||ids.size()>10000||!operation.contains("dx")||!operation["dx"].is_number()||!operation.contains("dy")||!operation["dy"].is_number())throw std::invalid_argument("ITEM_TRANSLATION_INVALID");
   const double dx=operation["dx"].get<double>(),dy=operation["dy"].get<double>();
   if(!std::isfinite(dx)||!std::isfinite(dy))throw std::invalid_argument("ITEM_TRANSLATION_INVALID");
   std::unordered_set<std::string> unique;
   for(const auto& value:ids){
    if(!value.is_string()||!unique.insert(value.get<std::string>()).second)throw std::invalid_argument("ITEM_TRANSLATION_INVALID");
    const auto id=value.get<std::string>();auto item=get(id);
    if(item.is_null()||item.value("kind","")=="connector"||item.value("type","")=="chat_room")throw std::invalid_argument("ITEM_NOT_FOUND");
    if(item.value("kind","")=="stroke")for(auto& point:item["points"]){point["x"]=point["x"].get<double>()+dx;point["y"]=point["y"].get<double>()+dy;}
    else{item["x"]=item.value("x",0.0)+dx;item["y"]=item.value("y",0.0)+dy;}
    validate(item);result.items[id]=item;
   }
   result.translations.push_back({{"ids",ids},{"dx",dx},{"dy",dy}});
  }else{
   result.only_translations=false;const auto id=operation.value("id","");auto old=get(id);
   if(action=="delete"){
    if(!old.is_null()&&old.value("type","")=="chat_room")throw std::invalid_argument("CHAT_QUERY_REQUIRED");
    result.items[id]=nullptr;continue;
   }
   if(action!="create"&&action!="patch")throw std::invalid_argument("QUERY_ACTION_INVALID");
   if(action=="create"&&!old.is_null())throw std::invalid_argument("ITEM_ALREADY_EXISTS");
   if(action=="patch"&&old.is_null())throw std::invalid_argument("ITEM_NOT_FOUND");
   const auto patch=operation.value("patch",Json::object()),unset=operation.value("unset",Json::array());
   if(!patch.is_object()||!unset.is_array())throw std::invalid_argument("ITEM_PATCH_INVALID");
   auto item=old.is_null()?Json::object():old;item.update(patch);
   for(const auto& field:unset){if(!field.is_string()||field.get<std::string>().rfind("__",0)==0)throw std::invalid_argument("ITEM_PATCH_INVALID");item.erase(field.get<std::string>());}
   if(!old.is_null()&&old.value("kind","")!=item.value("kind",""))throw std::invalid_argument("ITEM_KIND_INVALID");
   validate(item);item=queryUpdatedItem(old,item,groups,admin);
   const auto kind=item.value("kind","");
   if(!old.is_null()&&(kind=="text"||kind=="note"||kind=="code")){
    if(old.contains("automerge_snapshot"))item["automerge_snapshot"]=old["automerge_snapshot"];
    Json merged=Json::array();std::unordered_set<std::string> seen;
    for(const auto& source:{old.value("automerge_changes",Json::array()),item.value("automerge_changes",Json::array())})for(const auto& change:source)if(seen.insert(change["change"].get<std::string>()).second)merged.push_back(change);
    item["automerge_changes"]=std::move(merged);
   }
   result.items[id]=item;
  }
  if(result.items.size()>20000)throw std::invalid_argument("QUERY_BATCH_INVALID");
 }
 // Resolve connector endpoints after all mutations, so a batch can create its graph.
 for(const auto& entry:result.items.items())if(!entry.value().is_null()&&entry.value().value("kind","")=="connector"){
  const auto from=entry.value().value("from",""),to=entry.value().value("to","");
  if(from==to||!validQueryItemId(from)||!validQueryItemId(to))throw std::invalid_argument("ITEM_CONNECTOR_INVALID");
  for(const auto& id:{from,to}){
   const auto endpoint=result.items.contains(id)?result.items[id]:read(id).value_or(Json());
   if(endpoint.is_null()||!queryCanAccess(endpoint,groups,admin)||endpoint.value("kind","")=="connector"||endpoint.value("kind","")=="host-tombstone")throw std::invalid_argument("ITEM_CONNECTOR_INVALID");
  }
 }
 if(result.items.dump().size()>8*1024*1024)throw std::invalid_argument("QUERY_BATCH_TOO_LARGE");
 return result;
}
}
