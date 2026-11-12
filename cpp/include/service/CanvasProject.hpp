#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
#include <functional>
#include <optional>
#include <unordered_set>
#include <string>
namespace agora::project {
using Json=nlohmann::json;
inline void invalid(const char* reason="PROJECT_DATA_INVALID"){throw std::invalid_argument(reason);}
inline bool str(const Json& j,const char* key,std::size_t limit){return j.contains(key)&&j[key].is_string()&&j[key].get_ref<const std::string&>().size()<=limit;}
inline bool date(const std::string& v){if(v.size()!=10||v[4]!='-'||v[7]!='-')return false;for(std::size_t i=0;i<v.size();i++)if(i!=4&&i!=7&&(v[i]<'0'||v[i]>'9'))return false;const int y=std::stoi(v.substr(0,4)),m=std::stoi(v.substr(5,2)),d=std::stoi(v.substr(8,2));const int days[]={0,31,28,31,30,31,30,31,31,30,31,30,31};return m>=1&&m<=12&&d>=1&&d<=days[m]+(m==2&&y%4==0&&(y%100!=0||y%400==0));}
inline bool dates(const Json& j,const char* a,const char* b,bool optional){if(!str(j,a,10)||!str(j,b,10))return false;auto x=j[a].get<std::string>(),y=j[b].get<std::string>();return (optional&&x.empty()||date(x))&&(optional&&y.empty()||date(y))&&(x.empty()||y.empty()||x<=y);}
inline void config(const Json& c){
 if(!c.is_object()||!c.contains("statuses")||!c["statuses"].is_array()||c["statuses"].size()<2||c["statuses"].size()>12||!c.contains("sprints")||!c["sprints"].is_array()||c["sprints"].size()>200||!c.contains("milestones")||!c["milestones"].is_array()||c["milestones"].size()>200||c.dump().size()>262144)invalid();
 bool todo=false,done=false;int active=0;
 for(const auto* key:{"statuses","sprints","milestones"}){std::unordered_set<std::string> ids;for(const auto& row:c[key]){if(!str(row,"id",128)||row["id"].get<std::string>().empty()||!ids.insert(row["id"].get<std::string>()).second)invalid();}}
 for(const auto& s:c["statuses"]){if(!str(s,"label",320)||s["label"].get<std::string>().find_first_not_of(" \t\r\n")==std::string::npos||!s.contains("approval")||!s["approval"].is_boolean())invalid();todo|=s["id"]=="todo";done|=s["id"]=="done";}if(!todo||!done)invalid();
 for(const auto& s:c["sprints"]){if(!str(s,"title",640)||s["title"].get<std::string>().empty()||!str(s,"goal",8000)||!str(s,"state",16)||!dates(s,"start","end",false))invalid();auto state=s["state"].get<std::string>();if(state!="planned"&&state!="active"&&state!="closed")invalid();if(state=="active")active++;}if(active>1)invalid();
 for(const auto& m:c["milestones"])if(!str(m,"title",640)||m["title"].get<std::string>().empty()||!str(m,"date",10)||!date(m["date"].get<std::string>()))invalid();
}
inline void validate(const Json& item){
 if(item.value("kind","")!="project")return;
 if(!str(item,"projectType",16)||!str(item,"title",1200))invalid();
 if(item["projectType"]=="board"){if(!item.contains("projectConfig"))invalid();config(item["projectConfig"]);return;}
 if(item["projectType"]!="issue")invalid();
 for(const auto* field:{"projectBoard","projectStatus","projectRequestedStatus","projectParent","projectSprint"})if(!str(item,field,128))invalid();
 if(item["projectBoard"].get<std::string>().empty()||!str(item,"projectIssueType",16)||!str(item,"projectPriority",16)||!str(item,"projectAssignee",640)||!str(item,"text",80000)||!dates(item,"projectStart","projectDue",true))invalid();
 static const std::unordered_set<std::string> types={"Epic","Story","Task","Bug","Sub-task"},priorities={"critical","high","medium","low"};
 if(!types.count(item["projectIssueType"].get<std::string>())||!priorities.count(item["projectPriority"].get<std::string>())||(item["projectIssueType"]=="Sub-task"&&item["projectParent"]==""))invalid();
 if(!item.contains("projectPoints")||!item["projectPoints"].is_number_integer()||item["projectPoints"].get<double>()<0||item["projectPoints"].get<double>()>1000||!item.contains("projectRank")||!item["projectRank"].is_number()||!std::isfinite(item["projectRank"].get<double>())||std::abs(item["projectRank"].get<double>())>1e12)invalid();
 if(!item.contains("projectDependencies")||!item["projectDependencies"].is_array()||item["projectDependencies"].size()>64)invalid();std::unordered_set<std::string> seen;
 for(const auto& id:item["projectDependencies"])if(!id.is_string()||id.get<std::string>().empty()||id.get<std::string>().size()>128||!seen.insert(id.get<std::string>()).second)invalid();
 if(!item.contains("projectHistory")||!item["projectHistory"].is_array()||item["projectHistory"].size()>200)invalid();
 for(const auto& h:item["projectHistory"])if(!str(h,"sprint",128)||!h.contains("points")||!h["points"].is_number_integer()||h["points"].get<double>()<0||h["points"].get<double>()>1000||!h.contains("done")||!h["done"].is_boolean())invalid();
}
inline void authority(const Json& old,const Json& item,bool admin){
 if(item.value("kind","")!="project"||admin)return;
 if(item.value("projectType","")=="board"){
  if(!old.is_null()&&old.value("projectConfig",Json())!=item["projectConfig"])invalid("PROJECT_MANAGE_DENIED");
  if(old.is_null()&&(!item["projectConfig"]["sprints"].empty()||!item["projectConfig"]["milestones"].empty()||item["projectConfig"]["statuses"]!=Json::array({{{"id","todo"},{"label","To Do"},{"approval",false}},{{"id","progress"},{"label","In Progress"},{"approval",false}},{{"id","done"},{"label","Done"},{"approval",false}}})))invalid("PROJECT_MANAGE_DENIED");
 }else if(item.value("projectHistory",Json::array())!=(old.is_null()?Json::array():old.value("projectHistory",Json::array())))invalid("PROJECT_MANAGE_DENIED");
 if(!old.is_null()&&old.value("projectType","")!=item.value("projectType",""))invalid();
}
template<typename Lookup> inline void links(const std::string& id,const Json& item,const Json& old,Lookup lookup,bool admin){
 if(item.value("kind","")!="project"||item.value("projectType","")!="issue")return;
 const auto boardId=item["projectBoard"].get<std::string>();const auto board=lookup(boardId);
 if(board.is_null()||board.value("kind","")!="project"||board.value("projectType","")!="board")invalid("PROJECT_BOARD_NOT_FOUND");
 const auto& c=board["projectConfig"];bool status=false,requested=item["projectRequestedStatus"]=="";
 for(const auto& s:c["statuses"]){if(s["id"]==item["projectStatus"]){status=true;if(s["approval"]==true&&!admin&&(old.is_null()||old.value("projectStatus","")!=item["projectStatus"]||old.value("projectBoard","")!=boardId))invalid("PROJECT_APPROVAL_REQUIRED");}if(s["id"]==item["projectRequestedStatus"])requested=true;}
 if(!status||!requested)invalid("PROJECT_STATUS_INVALID");
 if(item["projectSprint"]!=""){bool found=false;for(const auto& s:c["sprints"])if(s["id"]==item["projectSprint"]){found=true;if(s["state"]=="closed"&&(old.is_null()||old.value("projectSprint","")!=item["projectSprint"]))invalid("PROJECT_SPRINT_CLOSED");}if(!found)invalid("PROJECT_SPRINT_NOT_FOUND");}
 for(const auto* field:{"projectDependencies","projectParent"}){
  std::unordered_set<std::string> path,done;std::size_t count=0;
  std::function<void(const std::string&,int)> visit=[&](const std::string& current,int depth){if(depth>128||path.count(current))invalid("PROJECT_CYCLE");if(done.count(current))return;if(++count>10000)invalid("PROJECT_GRAPH_TOO_LARGE");auto node=lookup(current);if(node.is_null()||node.value("projectType","")!="issue"||node.value("projectBoard","")!=boardId)invalid("PROJECT_REFERENCE_INVALID");path.insert(current);if(std::string(field)=="projectParent"){auto parent=node.value(field,"");if(!parent.empty())visit(parent,depth+1);}else for(const auto& next:node[field])visit(next.template get<std::string>(),depth+1);path.erase(current);done.insert(current);};visit(id,0);
 }
 if(item["projectParent"]!=""){const auto parent=lookup(item["projectParent"].get<std::string>());if(item["projectIssueType"]=="Epic"||parent.value("projectIssueType","")=="Sub-task"||(item["projectIssueType"]=="Story"&&parent.value("projectIssueType","")!="Epic"))invalid("PROJECT_HIERARCHY_INVALID");}
}
}
