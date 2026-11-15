#include "service/CanvasBrokerOperations.hpp"
#include "TestSupport.hpp"
using Json=nlohmann::json;
void projects(){
 Json statuses=Json::array({{{"id","todo"},{"label","To Do"},{"approval",false}},{{"id","progress"},{"label","In Progress"},{"approval",false}},{{"id","done"},{"label","Done"},{"approval",false}}});
 Json b={{"kind","project"},{"projectType","board"},{"title","Project"},{"permission","writers"},{"x",0},{"y",0},{"width",1},{"height",.68},{"projectConfig",{{"statuses",statuses},{"sprints",Json::array()},{"milestones",Json::array()}}}};
 Json i={{"kind","project"},{"projectType","issue"},{"title","Task"},{"text",""},{"permission","writers"},{"x",0},{"y",0},{"width",.28},{"height",.14},{"projectBoard","b"},{"projectIssueType","Task"},{"projectStatus","todo"},{"projectRequestedStatus",""},{"projectAssignee",""},{"projectPriority","medium"},{"projectStart",""},{"projectDue",""},{"projectParent",""},{"projectDependencies",Json::array()},{"projectSprint",""},{"projectPoints",3},{"projectRank",1024},{"projectHistory",Json::array()}};
 std::map<std::string,Json> store;auto read=[&](const std::string& id)->std::optional<Json>{return store.count(id)?std::optional<Json>(store[id]):std::nullopt;};
 auto op=[&](const std::string& id,Json patch){return Json{{"action",store.count(id)?"patch":"create"},{"id",id},{"patch",patch}};};
 auto run=[&](Json ops,bool admin=false){auto r=agora::broker::prepare(ops,read,{"writers"},admin);for(auto& e:r.items.items())if(e.value().is_null())store.erase(e.key());else store[e.key()]=e.value();return r;};
 auto rejects=[&](Json ops,const std::string& code){bool denied=false;try{run(ops);}catch(const std::invalid_argument& e){denied=code==e.what();if(!denied)std::cerr<<"Expected "<<code<<" got "<<e.what()<<'\n';}AGORA_CHECK(denied);};
 rejects(Json::array({op("b",b)}),"PROJECT_MANAGE_DENIED");
 run(Json::array({op("b",b),op("a",i),op("c",i)}),true);
 auto config=b["projectConfig"];config["statuses"][2]["approval"]=true;
 run(Json::array({op("b",{{"projectConfig",config}})}));run(Json::array({op("b",{{"projectConfig",config}})}),true);
 rejects(Json::array({op("a",{{"projectStatus","done"}})}),"PROJECT_APPROVAL_REQUIRED");
 run(Json::array({op("a",{{"projectRequestedStatus","done"}})}));AGORA_CHECK(store["a"]["projectStatus"]=="todo");run(Json::array({op("a",{{"projectStatus","done"},{"projectRequestedStatus",""}})}),true);
 run(Json::array({op("c",{{"projectDependencies",Json::array({"a"})}})}));rejects(Json::array({op("a",{{"projectDependencies",Json::array({"c"})}})}),"PROJECT_CYCLE");
 rejects(Json::array({op("a",{{"projectDue","2026-02-30"}})}),"PROJECT_DATA_INVALID");rejects(Json::array({op("a",{{"projectPoints",1099511627776LL}})}),"PROJECT_DATA_INVALID");
 rejects(Json::array({op("a",{{"projectBoard","missing"}})}),"PROJECT_BOARD_NOT_FOUND");run(Json::array({op("a",{{"projectHistory",Json::array({{{"sprint","s"},{"points",3},{"done",true}}})}})}));
 config["sprints"]=Json::array({{{"id","s"},{"title","Sprint"},{"goal","Ship"},{"start","2026-11-10"},{"end","2026-11-24"},{"state","closed"}}});run(Json::array({op("b",{{"projectConfig",config}})}),true);rejects(Json::array({op("a",{{"projectSprint","s"}})}),"PROJECT_SPRINT_CLOSED");
 const std::unordered_set<std::string> empty,readable{"writers"};bool denied=false;try{agora::broker::prepare(Json::array({op("a",{{"title","Edit"}})}),read,empty,false,&readable);}catch(const std::invalid_argument& e){denied=std::string(e.what())=="ITEM_WRITE_DENIED";}AGORA_CHECK(denied);
 AGORA_CHECK(agora::project::date("2024-02-29"));AGORA_CHECK(!agora::project::date("2025-02-29"));
}
int main(){return runTest("project workflow, dependencies, lifecycle and server ACL",projects);}
