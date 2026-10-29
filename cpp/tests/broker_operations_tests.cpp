#include "service/CanvasBrokerOperations.hpp"
#include "TestSupport.hpp"
using Json=nlohmann::json;
void operations(){
 const Json shape{{"kind","shape"},{"shapeType","rectangle"},{"x",0},{"y",0},{"width",1},{"height",1},{"permission","writers"}};
 std::map<std::string,Json> store{{"box",shape}};
 auto read=[&](const std::string& id)->std::optional<Json>{return store.count(id)?std::optional<Json>(store[id]):std::nullopt;};
 auto run=[&](Json ops){return agora::broker::prepare(ops,read,{"writers"},false);};
 auto reject=[&](Json ops){bool rejected=false;try{run(ops);}catch(const std::invalid_argument&){rejected=true;}AGORA_CHECK(rejected);};
 auto created=run(Json::array({{{"action","create"},{"id","new"},{"patch",shape}}}));
 AGORA_CHECK(created.items["new"]==shape&&created.expected["new"]==-1);
 const Json chart{{"kind","chart"},{"chartType","bar"},{"columns",Json::array({"label","value"})},{"rows",Json::array({Json::array({"A","12"})})},{"x",0},{"y",0},{"width",1},{"height",1},{"permission","writers"}};
 auto chart_created=run(Json::array({{{"action","create"},{"id","chart"},{"patch",chart}}}));AGORA_CHECK(chart_created.items["chart"]==chart);
 auto bad_chart=chart;bad_chart["chartType"]="invalid";reject(Json::array({{{"action","create"},{"id","badchart"},{"patch",bad_chart}}}));
 bad_chart=chart;bad_chart["rows"]=Json::array({Json::array({"only one"})});reject(Json::array({{{"action","create"},{"id","badchart"},{"patch",bad_chart}}}));
 auto volume=shape;volume["shapeType"]="volume-cube";volume["volumeStyle"]="wireframe";auto volume_created=run(Json::array({{{"action","create"},{"id","volume"},{"patch",volume}}}));AGORA_CHECK(volume_created.items["volume"]==volume);
 volume["volumeStyle"]="invalid";reject(Json::array({{{"action","create"},{"id","volume"},{"patch",volume}}}));
 auto moved=run(Json::array({{{"action","translate"},{"ids",Json::array({"box"})},{"dx",3},{"dy",4}}}));
 AGORA_CHECK(moved.items["box"]["x"]==3&&moved.items["box"]["y"]==4&&moved.only_translations);
 AGORA_CHECK(store["box"]==shape);
 auto patched=run(Json::array({{{"action","patch"},{"id","box"},{"patch",{{"color","red"}}}}}));
 AGORA_CHECK(patched.items["box"]["color"]=="red"&&!patched.only_translations);
 auto removed=run(Json::array({{{"action","delete"},{"id","box"}}}));AGORA_CHECK(removed.items["box"].is_null());
 auto executor=shape; executor["runtimeRole"]="executor";
 auto accepted=run(Json::array({{{"action","create"},{"id","executor"},{"patch",executor}}}));
 AGORA_CHECK(accepted.items["executor"]["runtimeRole"]=="executor");
 auto output=shape;output["runtimeRole"]="output";
 output["runtimeResults"]=Json::array({{{"runId","run-1"},{"codeId","code-1"},{"createdAt",123},{"exitCode",0},{"output","hello\n"},{"timedOut",false},{"truncated",false}}});
 auto saved=run(Json::array({{{"action","create"},{"id","result"},{"patch",output}}}));
 AGORA_CHECK(saved.items["result"]["runtimeResults"][0]["output"]=="hello\n");
 output["runtimeResults"][0]["unknown"]=true;
 reject(Json::array({{{"action","create"},{"id","bad-result"},{"patch",output}}}));
 executor["runtimeRole"]="arbitrary";
 reject(Json::array({{{"action","create"},{"id","invalid-role"},{"patch",executor}}}));
 reject(Json::array({{{"action","create"},{"id","box"},{"patch",shape}}}));
 reject(Json::array({{{"action","patch"},{"id","missing"},{"patch",{{"x",1}}}}}));
 reject(Json::array({{{"action","patch"},{"id","box"},{"patch",{{"permission","admins"}}}}}));
 reject(Json::array({{{"action","patch"},{"id","box"},{"patch",{{"__broker_revision",1}}}}}));
 reject(Json::array({{{"action","patch"},{"id","box"},{"patch",{{"bendPoints",Json::array({{{"x",1},{"y",1}}})},{"bendPointCount",3}}}}}));
 reject(Json::array({{{"action","translate"},{"ids",Json::array({"box","box"})},{"dx",1},{"dy",1}}}));
 Json connector{{"kind","connector"},{"from","box"},{"to","new"},{"permission","writers"}};
 auto graph=run(Json::array({{{"action","create"},{"id","wire"},{"patch",connector}},{{"action","create"},{"id","new"},{"patch",shape}}}));
 AGORA_CHECK(graph.items.size()==2);
 reject(Json::array({{{"action","create"},{"id","wire"},{"patch",connector}}}));
 Json text{{"kind","text"},{"text","a"},{"permission","writers"},{"automerge_snapshot","base"},{"automerge_changes",Json::array({{{"field","text"},{"change","a"}}})}};
 store["text"]=text;
 auto merged=run(Json::array({{{"action","patch"},{"id","text"},{"patch",{{"text","b"},{"automerge_snapshot","other"},{"automerge_changes",Json::array({{{"field","text"},{"change","b"}}})}}}}}));
 AGORA_CHECK(merged.items["text"]["automerge_snapshot"]=="base"&&merged.items["text"]["automerge_changes"].size()==2);
 store["box"]["__host_version"]=Json::array({1,2});store["box"]["__broker_revision"]=7;
 auto migrated=run(Json::array({{{"action","patch"},{"id","box"},{"patch",{{"x",2}}}}}));
 AGORA_CHECK(!migrated.items["box"].contains("__host_version")&&migrated.expected["box"]==7);
}
int main(){return runTest("server authoritative operations, ACL, graph and CRDT union",operations);}
