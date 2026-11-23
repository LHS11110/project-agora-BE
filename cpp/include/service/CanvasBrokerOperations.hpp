#include "service/CanvasTextDelta.hpp"
#include "service/CanvasPresentation.hpp"
#include "service/CanvasProject.hpp"
#include "service/CanvasNotebookValidation.hpp"
#include "service/CanvasValueValidation.hpp"
#pragma once
#include "service/CanvasQueryRules.hpp"
#include "service/CanvasQuestion.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <cmath>
#include <optional>
namespace agora::broker {
using Json=nlohmann::json;
struct Mutation {Json items=Json::object(),previous=Json::object(),expected=Json::object(),translations=Json::array();bool only_translations=true;};
inline const std::unordered_set<std::string>& objectFields(){
 static const std::unordered_set<std::string> fields={"timerDuration","timerRemaining","timerDeadline","frameId","animationPriority","frameOrder","frameLinkPriority","portfolioCursor","animation","animationPlayback","portfolioPages","portfolioDescription","projectType","projectBoard","projectIssueType","projectStatus","projectRequestedStatus","projectAssignee","projectPriority","projectStart","projectDue","projectParent","projectDependencies","projectSprint","projectPoints","projectRank","projectHistory","projectConfig","notebookCells","notebookResults","notebookCellIds","notebookId","notebookCell","notebookResult","valueType","valueNumber","valueMin","valueMax","valueStep","valueText","valueOptions","pdfPage","mediaState","mediaLoop","runtimeState","questionType","questionPrompt","questionOptions","questionAnswer","questionSubmitted","questionMaxScore","questionScore","questionFeedback","questionVersion","questionGradedVersion","kind","aiGenerated","text","code","formula","format","filename","language","shapeType","visualStyle","volumeStyle","chartType","color","fill","permission","groupId","src","url","title","mediaType","from","to","fromPort","toPort","startHead","endHead","brush","embedUrl","videoId","renderMode","label","labelMode","groupTitle","thumbnail","cornerStyle","runtimeRole","runtimeResults","shadowBlur","shadowOpacity","shadowOffsetX","shadowOffsetY","depth","x","y","width","height","rotation","fontSize","strokeWidth","bend","opacity","contentScale","contentScaleX","contentScaleY","bendPointCount","headSize","vertexCount","fillOpacity","sketchFillOpacity","points","rows","columns","simulatePressure","complete","strokeEnabled","shadowEnabled","locked","imageFrameEnabled","bendPoints","members","channelType","name","order","categoryId","automerge_snapshot","automerge_changes"};
 return fields;
}
inline void validate(const Json& item){
 static const std::unordered_set<std::string> kinds={"frame","portfolio","project","text","note","code","math","shape","table","chart","question","value","image","link","connector","stroke","pdf","user-group","channel"};
 const auto& fields=objectFields();
 if(!item.is_object()||!kinds.count(item.value("kind","")))throw std::invalid_argument("ITEM_KIND_INVALID");
 // Chat/call channels and channel categories are footprint-free items; a text
 // channel's messages are its comment thread (see CanvasQueryService chat).
 const bool channel=item.value("kind","")=="channel";
 if(channel||item.contains("channelType")||item.contains("name")||item.contains("order")||item.contains("categoryId")){
  if(!channel||!item.contains("channelType")||!item["channelType"].is_string())throw std::invalid_argument("ITEM_CHANNEL_INVALID");
  const auto type=item["channelType"].get<std::string>();
  if(type!="text"&&type!="voice"&&type!="category")throw std::invalid_argument("ITEM_CHANNEL_INVALID");
  if(!item.contains("name")||!item["name"].is_string()||item["name"].get<std::string>().empty()||item["name"].get<std::string>().size()>200)throw std::invalid_argument("ITEM_CHANNEL_INVALID");
  if(!item.contains("order")||!item["order"].is_number())throw std::invalid_argument("ITEM_CHANNEL_INVALID");
  if(item.contains("categoryId")&&(type=="category"||!item["categoryId"].is_string()||item["categoryId"].get<std::string>().empty()||item["categoryId"].get<std::string>().size()>128))throw std::invalid_argument("ITEM_CHANNEL_INVALID");
 }
 if(item.contains("aiGenerated")&&!item["aiGenerated"].is_boolean())throw std::invalid_argument("ITEM_FIELD_INVALID");
 agora::presentation::validate(item);
 for(const auto& field:item.items()){
  if(!fields.count(field.key()))throw std::invalid_argument("ITEM_FIELD_INVALID");
  if(field.value().is_number()&&!std::isfinite(field.value().get<double>()))throw std::invalid_argument("ITEM_NUMBER_INVALID");
 }
 for(const auto* field:{"valueNumber","valueMin","valueMax","valueStep","pdfPage","x","y","width","height","rotation","fontSize","strokeWidth","depth","shadowBlur","shadowOpacity","shadowOffsetX","shadowOffsetY","bend","opacity","contentScale","contentScaleX","contentScaleY","bendPointCount","headSize","vertexCount","fillOpacity","sketchFillOpacity"})if(item.contains(field)&&!item[field].is_number())throw std::invalid_argument("ITEM_NUMBER_INVALID");
 for(const auto* field:{"width","height","fontSize","strokeWidth"})if(item.contains(field)&&item[field].get<double>()<=0)throw std::invalid_argument("ITEM_SIZE_INVALID");
 if(item.contains("mediaLoop")&&(item.value("kind","")!="link"||!item["mediaLoop"].is_boolean()))throw std::invalid_argument("ITEM_MEDIA_STATE_INVALID");
 if(item.contains("locked")&&!item["locked"].is_boolean())throw std::invalid_argument("ITEM_LOCK_INVALID");
 if(item.contains("volumeStyle")&&(!item["volumeStyle"].is_string()||(item["volumeStyle"]!="solid"&&item["volumeStyle"]!="wireframe")))throw std::invalid_argument("ITEM_VOLUME_STYLE_INVALID");
 if(item.contains("runtimeRole")&&(item.value("kind","")!="shape"||!item["runtimeRole"].is_string()||(item["runtimeRole"]!="executor"&&item["runtimeRole"]!="output"&&item["runtimeRole"]!="terminal"&&item["runtimeRole"]!="notebook"&&item["runtimeRole"]!="notebook-cell"&&item["runtimeRole"]!="timer")))throw std::invalid_argument("ITEM_RUNTIME_ROLE_INVALID");
 const bool timer=item.value("runtimeRole","")=="timer";
 if(timer||item.contains("timerDuration")||item.contains("timerRemaining")||item.contains("timerDeadline")){
  if(!timer)throw std::invalid_argument("ITEM_TIMER_INVALID");
  for(const auto* field:{"timerDuration","timerRemaining","timerDeadline"})
   if(!item.contains(field)||!item[field].is_number_integer()||item[field].get<double>()<0||item[field].get<double>()>9007199254740991.0)
    throw std::invalid_argument("ITEM_TIMER_INVALID");
  const auto duration=item["timerDuration"].get<double>();
  if(duration<1||duration>86400||item["timerRemaining"].get<double>()>duration*1000)
   throw std::invalid_argument("ITEM_TIMER_INVALID");
 }
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
 agora::notebook::validate(item);
 agora::question::validate(item);
 agora::value::validate(item);
 agora::project::validate(item);
 agora::shared::validate(item);
 if(item.contains("pdfPage")&&(item.value("kind","")!="pdf"||!item["pdfPage"].is_number_integer()||item["pdfPage"].get<long long>()<1||item["pdfPage"].get<long long>()>1000000))throw std::invalid_argument("ITEM_PDF_PAGE_INVALID");
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
 const std::unordered_set<std::string>& groups,bool admin,const std::unordered_set<std::string>* readable=nullptr,const std::unordered_set<std::string>* executable=nullptr,const std::unordered_set<std::string>* memberships=nullptr,bool agent_creation=false){
 if(!operations.is_array()||operations.empty()||operations.size()>512)throw std::invalid_argument("QUERY_BATCH_INVALID");
 Mutation result;
 auto get=[&](const std::string& id)->Json{
  if(!validQueryItemId(id))throw std::invalid_argument("ITEM_ID_INVALID");
  if(result.items.contains(id))return result.items[id];
  const auto loaded=read(id);Json old=loaded.value_or(Json());
  if(!old.is_null()&&!queryCanAccess(old,readable?*readable:groups,admin))throw std::invalid_argument("ITEM_ACCESS_DENIED");
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
   if(operation.contains("start")||operation.contains("end")){
    for(const auto* endpoint:{"start","end"})if(!operation.contains(endpoint)||!operation[endpoint].is_object()||operation[endpoint].size()!=2||!operation[endpoint].contains("x")||!operation[endpoint].contains("y")||!operation[endpoint]["x"].is_number()||!operation[endpoint]["y"].is_number()||!std::isfinite(operation[endpoint]["x"].get<double>())||!std::isfinite(operation[endpoint]["y"].get<double>()))throw std::invalid_argument("ITEM_TRANSLATION_INVALID");
    if(std::abs(operation["end"]["x"].get<double>()-operation["start"]["x"].get<double>()-dx)>1e-7||std::abs(operation["end"]["y"].get<double>()-operation["start"]["y"].get<double>()-dy)>1e-7)throw std::invalid_argument("ITEM_TRANSLATION_INVALID");
   }
   std::unordered_set<std::string> unique;
   for(const auto& value:ids){
    if(!value.is_string()||!unique.insert(value.get<std::string>()).second)throw std::invalid_argument("ITEM_TRANSLATION_INVALID");
    const auto id=value.get<std::string>();auto item=get(id);
    if(item.is_null()||item.value("kind","")=="connector"||item.value("type","")=="chat_room")throw std::invalid_argument("ITEM_NOT_FOUND");
    if(!queryCanAccess(item,groups,admin))throw std::invalid_argument("ITEM_WRITE_DENIED");
    if(queryObjectLocked(item))throw std::invalid_argument("ITEM_LOCKED");
    if(item.value("kind","")=="stroke")for(auto& point:item["points"]){point["x"]=point["x"].get<double>()+dx;point["y"]=point["y"].get<double>()+dy;}
    else{item["x"]=item.value("x",0.0)+dx;item["y"]=item.value("y",0.0)+dy;}
    validate(item);result.items[id]=item;
   }
   Json translated={{"ids",ids},{"dx",dx},{"dy",dy}};if(operation.contains("start")){translated["start"]=operation["start"];translated["end"]=operation["end"];}result.translations.push_back(translated);
  }else{
   result.only_translations=false;const auto id=operation.value("id","");auto old=get(id);
   if(operation.contains("expected_revision")&&(!operation["expected_revision"].is_number_integer()||old.is_null()||operation["expected_revision"]!=result.expected[id]))throw std::invalid_argument("CANVAS_WRITE_CONFLICT");
   if(action=="delete"){
    if(!old.is_null()&&old.value("kind","")=="project"&&old.value("projectType","")=="board"&&!admin)throw std::invalid_argument("PROJECT_MANAGE_DENIED");
    if(!old.is_null()&&!queryCanAccess(old,groups,admin))throw std::invalid_argument("ITEM_WRITE_DENIED");
    if(queryObjectLocked(old))throw std::invalid_argument("ITEM_LOCKED");
    if(!old.is_null()&&old.value("type","")=="chat_room")throw std::invalid_argument("CHAT_QUERY_REQUIRED");
    result.items[id]=nullptr;continue;
   }
   if(action!="create"&&action!="patch")throw std::invalid_argument("QUERY_ACTION_INVALID");
   if(action=="create"&&!old.is_null())throw std::invalid_argument("ITEM_ALREADY_EXISTS");
   if(action=="patch"&&old.is_null())throw std::invalid_argument("ITEM_NOT_FOUND");
   const auto patch=operation.value("patch",Json::object()),unset=operation.value("unset",Json::array());
   if(!patch.is_object()||!unset.is_array())throw std::invalid_argument("ITEM_PATCH_INVALID");
   if(patch.contains("aiGenerated"))throw std::invalid_argument("ITEM_FIELD_INVALID");
   auto item=old.is_null()?Json::object():old;item.update(patch);
   if(action=="create"&&agent_creation)item["aiGenerated"]=true;
   if(operation.contains("notebook_structure")){
    const auto& structure=operation["notebook_structure"];
    if(action!="patch"||old.value("runtimeRole",std::string())!="notebook"||!old.contains("notebookCells")||patch.contains("notebookCells")||!structure.is_object()||structure.size()!=3||!structure.contains("order")||!structure["order"].is_array()||!structure.contains("insert")||!structure["insert"].is_array()||!structure.contains("remove")||!structure["remove"].is_array()||structure["order"].size()>32||structure["insert"].size()>32||structure["remove"].size()>32)throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
    std::map<std::string,Json> cells;for(const auto& cell:item["notebookCells"])cells[cell["id"].get<std::string>()]=cell;
    for(const auto& id:structure["remove"]){if(!id.is_string())throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");const auto key=id.get<std::string>();cells.erase(key);if(item.contains("notebookResults"))item["notebookResults"].erase(key);}
    for(const auto& cell:structure["insert"]){if(!cell.is_object()||!cell.contains("id")||!cell["id"].is_string()||cells.count(cell["id"].get<std::string>()))throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");cells[cell["id"].get<std::string>()]=cell;}
    Json ordered=Json::array();std::unordered_set<std::string> seen;
    for(const auto& id:structure["order"]){if(!id.is_string()||!seen.insert(id.get<std::string>()).second)throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");auto found=cells.find(id.get<std::string>());if(found!=cells.end()){ordered.push_back(found->second);cells.erase(found);}}
    for(const auto& entry:cells)ordered.push_back(entry.second);item["notebookCells"]=ordered;
   }
   if(operation.contains("notebook_results")){
    const auto& results=operation["notebook_results"];
    if(action!="patch"||old.value("runtimeRole",std::string())!="notebook"||patch.contains("notebookResults")||!results.is_object()||results.size()!=2||!results.contains("set")||!results["set"].is_object()||!results.contains("remove")||!results["remove"].is_array()||results["set"].size()>32||results["remove"].size()>32)throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
    if(!item.contains("notebookResults"))item["notebookResults"]=Json::object();
    item["notebookResults"].update(results["set"]);for(const auto& id:results["remove"]){if(!id.is_string())throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");item["notebookResults"].erase(id.get<std::string>());}
   }
   if(operation.contains("notebook_edits")){
    const auto& edits=operation["notebook_edits"];
    if(action!="patch"||old.value("runtimeRole",std::string())!="notebook"||!old.contains("notebookCells")||patch.contains("notebookCells")||!edits.is_array()||edits.size()>32)throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
    std::unordered_set<std::string> edited;
    for(const auto& edit:edits){
     if(!edit.is_object()||(edit.size()!=2&&edit.size()!=3)||!edit.contains("id")||!edit["id"].is_string()||!edit.contains("patch")||!edit["patch"].is_object())throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
     const auto cell_id=edit["id"].get<std::string>();if(!edited.insert(cell_id).second)throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
     bool found=false;
     for(auto& cell:item["notebookCells"])if(cell.value("id",std::string())==cell_id){
      for(const auto& field:edit["patch"].items())if(field.key()!="source"&&field.key()!="cellType")throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
      cell.update(edit["patch"]);if(edit.contains("source_delta")){if(edit["patch"].contains("source"))throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");cell["source"]=agora::textdelta::apply(cell.value("source",std::string()),edit["source_delta"]);}
      found=true;break;
     }
     if(!found)throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
    }
   }
   if(operation.contains("text_edits")){
    const auto& edits=operation["text_edits"];
    if(action!="patch"||!edits.is_object()||edits.empty()||edits.size()>2)throw std::invalid_argument("ITEM_TEXT_DELTA_INVALID");
    for(const auto& field:edits.items()){
     if((field.key()!="text"&&field.key()!="code")||patch.contains(field.key())||!old.contains(field.key())||!old[field.key()].is_string())throw std::invalid_argument("ITEM_TEXT_DELTA_INVALID");
     item[field.key()]=agora::textdelta::apply(old[field.key()].get<std::string>(),field.value());
    }
   }
   if(operation.contains("points_append")){
    // A stroke being drawn is saved ~30 times a second. Instead of the whole point
    // list, the client sends the points after the prefix the server already has.
    // `last` pins that prefix; a mismatch is a conflict the client rebases.
    const auto& append=operation["points_append"];
    if(action!="patch"||old.value("kind","")!="stroke"||patch.contains("points")||!append.is_object()||append.size()!=3
     ||!append.contains("from")||!append["from"].is_number_integer()||append["from"].get<long long>()<0||!append.contains("last")||!append["last"].is_object()
     ||!append.contains("points")||!append["points"].is_array()||!old.contains("points")||!old["points"].is_array())
     throw std::invalid_argument("ITEM_POINTS_DELTA_INVALID");
    const auto from=static_cast<std::size_t>(append["from"].get<long long>());
    const auto& base=old["points"];
    const auto same_number=[](const Json& a,const Json& b){return a.is_number()&&b.is_number()&&a.get<double>()==b.get<double>();};
    const auto same_point=[&](const Json& a,const Json& b){
     if(!a.is_object()||!b.is_object()||!a.contains("x")||!a.contains("y")||!b.contains("x")||!b.contains("y")||!same_number(a["x"],b["x"])||!same_number(a["y"],b["y"]))return false;
     return a.contains("pressure")==b.contains("pressure")&&(!a.contains("pressure")||same_number(a["pressure"],b["pressure"]));
    };
    if(from==0||from>base.size()||!same_point(base[from-1],append["last"]))throw std::invalid_argument("CANVAS_TEXT_CONFLICT");
    Json points=Json::array();
    for(std::size_t i=0;i<from;i++)points.push_back(base[i]);
    for(const auto& point:append["points"])points.push_back(point);
    item["points"]=std::move(points);
   }
   if(old.is_null()&&!item.contains("permission"))item["permission"]=memberships?*memberships:readable?*readable:groups;
   for(const auto& field:unset){if(!field.is_string()||field=="aiGenerated"||field.get<std::string>().rfind("__",0)==0)throw std::invalid_argument("ITEM_PATCH_INVALID");item.erase(field.get<std::string>());}
   if(!old.is_null()&&old.value("kind","")!=item.value("kind",""))throw std::invalid_argument("ITEM_KIND_INVALID");
   const bool output_only=action=="patch"&&old.is_object()&&old.value("runtimeRole","")=="output"&&patch.size()==1&&patch.contains("runtimeResults")&&unset.empty();
   const bool state_only=!operation.contains("notebook_edits")&&action=="patch"&&old.is_object()&&agora::shared::runtimeTarget(old)&&patch.size()==1&&patch.contains("runtimeState")&&unset.empty();
   const bool notebook_only=!operation.contains("notebook_edits")&&action=="patch"&&old.is_object()&&old.value("runtimeRole","")=="notebook"&&patch.size()==1&&patch.contains("notebookResults")&&unset.empty();
   const bool notebook_cell_result_only=action=="patch"&&old.is_object()&&old.value("runtimeRole","")=="notebook-cell"&&patch.size()==1&&patch.contains("notebookResult")&&unset.empty();
   const bool result_delta_only=action=="patch"&&old.is_object()&&old.value("runtimeRole",std::string())=="notebook"&&operation.contains("notebook_results")&&patch.empty()&&unset.empty();
   const bool execution_only=!operation.contains("notebook_structure")&&!operation.contains("text_edits")&&!operation.contains("notebook_edits")&&(output_only||state_only||notebook_only||notebook_cell_result_only||result_delta_only);
   const auto& writers=execution_only&&executable?*executable:groups;
   if(!old.is_null()&&!queryCanAccess(old,writers,admin))throw std::invalid_argument("ITEM_WRITE_DENIED");
   agora::question::validateRevision(old,item);validate(item);bool project_manager=false;
   if(item.value("kind","")=="project"){
    const auto board_id=item.value("projectBoard",std::string());
    const auto board=item.value("projectType","")=="board"?old:(result.items.contains(board_id)?result.items[board_id]:read(board_id).value_or(Json()));
    project_manager=board.is_object()&&board.value("kind","")=="project"&&board.value("projectType","")=="board"&&queryCanAccess(board,writers,admin);
   }
   agora::project::authority(old,item,admin,project_manager);agora::presentation::stamp(old,item);item=queryUpdatedItem(old,item,writers,admin,memberships?memberships:readable,execution_only);
   const auto kind=item.value("kind","");
   if(!old.is_null()&&(kind=="text"||kind=="note"||kind=="code")&&item.contains("automerge_snapshot")){
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
   if(endpoint.is_null()||!queryCanAccess(endpoint,readable?*readable:groups,admin)||endpoint.value("kind","")=="connector"||endpoint.value("kind","")=="host-tombstone")throw std::invalid_argument("ITEM_CONNECTOR_INVALID");
  }
 }
 for(const auto& entry:result.items.items())if(!entry.value().is_null()&&entry.value().value("kind","")=="project"){
  auto lookup=[&](const std::string& key)->Json{if(!validQueryItemId(key))throw std::invalid_argument("PROJECT_REFERENCE_INVALID");auto value=result.items.contains(key)?result.items[key]:read(key).value_or(Json());if(!value.is_null()&&!queryCanAccess(value,readable?*readable:groups,admin))throw std::invalid_argument("PROJECT_REFERENCE_ACCESS_DENIED");return value;};
  const auto old=result.previous.value(entry.key(),Json());agora::project::links(entry.key(),entry.value(),old,lookup,admin);
 }
 auto final_item=[&](const std::string& key)->Json{if(result.items.contains(key))return result.items[key];return read(key).value_or(Json());};
 for(const auto& entry:result.items.items())if(!entry.value().is_null()){
  const auto& value=entry.value();
  if(value.value("runtimeRole","")=="notebook"){
   const auto old=result.previous.value(entry.key(),Json());
   const auto old_ids=old.is_object()?old.value("notebookCellIds",Json::array()):Json::array();
   const auto new_ids=value.value("notebookCellIds",Json::array());
   const bool access_changed=old.is_null()||old.value("permission",Json())!=value.value("permission",Json())||old.value("groupId",Json())!=value.value("groupId",Json());
   if(old.is_null()||old_ids!=new_ids||access_changed){
    for(const auto& child_id:new_ids){
     const auto child=final_item(child_id.get<std::string>());
     if(child.is_null()||child.value("runtimeRole","")!="notebook-cell"||child.value("notebookId","")!=entry.key()||child.value("permission",Json())!=value.value("permission",Json())||child.value("groupId",Json())!=value.value("groupId",Json())||!queryCanAccess(child,readable?*readable:groups,admin))throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
    }
    for(const auto& child_id:old_ids)if(std::find(new_ids.begin(),new_ids.end(),child_id)==new_ids.end()){
     const auto child=final_item(child_id.get<std::string>());
     if(!child.is_null()&&child.value("runtimeRole","")=="notebook-cell"&&child.value("notebookId","")==entry.key())throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
    }
   }
  }
  if(value.value("runtimeRole","")=="notebook-cell"){
   const auto old=result.previous.value(entry.key(),Json());
   if(old.is_null()||old.value("notebookId","")!=value.value("notebookId","")||old.value("permission",Json())!=value.value("permission",Json())||old.value("groupId",Json())!=value.value("groupId",Json())){
    const auto parent_id=value.value("notebookId","");const auto parent=final_item(parent_id);
    if(parent.is_null()||parent.value("runtimeRole","")!="notebook")throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
    const auto ids=parent.value("notebookCellIds",Json::array());
    if(std::find(ids.begin(),ids.end(),entry.key())==ids.end()||value.value("permission",Json())!=parent.value("permission",Json())||value.value("groupId",Json())!=parent.value("groupId",Json()))throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
   }
  }
 }
 std::unordered_map<std::string,Json> frameReferences;
 for(const auto& entry:result.items.items())if(!entry.value().is_null()){
  const auto& value=entry.value();std::string frame;
  if(value.contains("frameId"))frame=value["frameId"].get<std::string>();
  if(value.value("kind","")=="portfolio"&&value.contains("portfolioCursor"))frame=value["portfolioCursor"]["frameId"].get<std::string>();
  if(!frame.empty()){if(!validQueryItemId(frame))throw std::invalid_argument("ITEM_PRESENTATION_INVALID");if(!result.items.contains(frame)&&!frameReferences.count(frame))frameReferences[frame]=read(frame).value_or(Json());auto target=result.items.contains(frame)?result.items[frame]:frameReferences.at(frame);if(target.is_null()||target.value("kind","")!="frame"||!queryCanAccess(target,readable?*readable:groups,admin))throw std::invalid_argument("FRAME_ACCESS_DENIED");}
 }
 if(result.items.dump().size()>8*1024*1024)throw std::invalid_argument("QUERY_BATCH_TOO_LARGE");
 return result;
}
}
