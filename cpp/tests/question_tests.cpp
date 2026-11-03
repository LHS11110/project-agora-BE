#include "service/CanvasBrokerOperations.hpp"
#include "TestSupport.hpp"
using Json=nlohmann::json;
void questions(){
 Json q={{"kind","question"},{"questionType","choice"},{"questionPrompt","2 + 2?"},{"questionOptions",Json::array({"3","4"})},{"questionAnswer",""},{"questionSubmitted",false},{"questionMaxScore",10},{"questionScore",-1},{"questionFeedback",""},{"questionVersion","v1"},{"questionGradedVersion",""},{"permission","writers"},{"x",0},{"y",0},{"width",.28},{"height",.4}};
 std::map<std::string,Json> store;
 auto read=[&](const std::string& id)->std::optional<Json>{return store.count(id)?std::optional<Json>(store[id]):std::nullopt;};
 auto apply=[&](const Json& patch){return agora::broker::prepare(Json::array({{{"action",store.count("q")?"patch":"create"},{"id","q"},{"patch",patch}}}),read,{"writers"},false);};
 auto rejects=[&](const Json& patch){bool rejected=false;try{apply(patch);}catch(const std::exception&){rejected=true;}AGORA_CHECK(rejected);};
 auto created=apply(q);store["q"]=created.items["q"];
 auto submitted=apply({{"questionAnswer","1"},{"questionSubmitted",true},{"questionVersion","v2"}});store["q"]=submitted.items["q"];
 auto graded=apply({{"questionScore",8.5},{"questionFeedback","Good"},{"questionGradedVersion","v2"}});AGORA_CHECK(graded.items["q"]["questionScore"]==8.5);store["q"]=graded.items["q"];
 rejects({{"questionPrompt",std::string(4001,'x')},{"questionVersion","v9"}});AGORA_CHECK(agora::question::utf16Length(u8"한글😀")==4);
 rejects({{"questionScore",11}});rejects({{"questionScore",-2}});rejects({{"questionAnswer","9"},{"questionVersion","v3"}});rejects({{"questionAnswer","0"}});rejects({{"questionOptions",Json::array({"only one"})}});
 auto changed=apply({{"questionAnswer","0"},{"questionScore",-1},{"questionFeedback",""},{"questionGradedVersion",""},{"questionVersion","v3"}});store["q"]=changed.items["q"];
 rejects({{"questionScore",10},{"questionGradedVersion","v2"}});
 // Undo may restore a coherent older question and its grade together.
 auto undo=apply(graded.items["q"]);AGORA_CHECK(undo.items["q"]["questionScore"]==8.5);
 const std::unordered_set<std::string> readable{"writers"},noWrites;
 bool denied=false;try{agora::broker::prepare(Json::array({{{"action","patch"},{"id","q"},{"patch",{{"questionScore",3},{"questionGradedVersion","v3"}}}}}),read,noWrites,false,&readable);}catch(const std::invalid_argument& e){denied=std::string(e.what())=="ITEM_WRITE_DENIED";}AGORA_CHECK(denied);
 auto subjective=apply({{"questionType","short-answer"},{"questionAnswer","Explanation"},{"questionVersion","v4"}});AGORA_CHECK(subjective.items["q"]["questionType"]=="short-answer");
 store["q"]["locked"]=true;rejects({{"questionScore",3},{"questionGradedVersion","v3"}});
}
int main(){return runTest("shared questions, manual grading, revisions, ACL and undo",questions);}
