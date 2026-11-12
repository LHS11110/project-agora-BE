#include "service/CanvasBrokerOperations.hpp"
#include "TestSupport.hpp"
using Json=nlohmann::json;
void presentation(){Json config={{"effect","float"},{"duration",1000},{"delay",0},{"loop",false},{"easing","ease"},{"distance",40}},item={{"kind","shape"},{"shapeType","rectangle"},{"x",0},{"y",0},{"width",.2},{"height",.2},{"permission","writers"},{"animation",config},{"animationPlayback",{{"runId","one"},{"action","play"},{"startedAt",0},{"config",config}}}};auto r=agora::broker::prepare(Json::array({{{"action","create"},{"id","shape"},{"patch",item}}}),[](const std::string&)->std::optional<Json>{return std::nullopt;},{"writers"},false);AGORA_CHECK(r.items["shape"]["animationPlayback"]["startedAt"].get<long long>()>1000000000000LL);item["animation"]["duration"]=-1;bool rejected=false;try{agora::broker::validate(item);}catch(const std::invalid_argument&){rejected=true;}AGORA_CHECK(rejected);Json p={{"kind","portfolio"},{"title","Portfolio"},{"portfolioDescription",""},{"portfolioPages",Json::array({{{"id","scene"},{"title","Project"},{"notes","Overview"},{"objectIds",Json::array({"shape"})}}})}};agora::broker::validate(p);p["portfolioPages"][0]["objectIds"].push_back("shape");rejected=false;try{agora::broker::validate(p);}catch(const std::invalid_argument&){rejected=true;}AGORA_CHECK(rejected);}
void frames(){
 Json f={{"kind","frame"},{"title","Frame"},{"text",""},{"frameOrder",0},{"rotation",0},{"x",0},{"y",0},{"width",.8},{"height",.72},{"permission","writers"}};
 Json config={{"effect","fade"},{"duration",1000},{"delay",0},{"loop",false},{"easing","ease"},{"distance",40}};
 Json a={{"kind","shape"},{"shapeType","rectangle"},{"x",.1},{"y",.1},{"width",.2},{"height",.1},{"permission","writers"},{"frameId","f"},{"animationPriority",0},{"animation",config}};
 std::map<std::string,Json> store;auto read=[&](const std::string& id)->std::optional<Json>{return store.count(id)?std::optional<Json>(store[id]):std::nullopt;};
 auto run=[&](Json operations){auto m=agora::broker::prepare(operations,read,{"writers"},false);for(const auto& e:m.items.items())store[e.key()]=e.value();return m;};
 run(Json::array({{{"action","create"},{"id","f"},{"patch",f}},{{"action","create"},{"id","a"},{"patch",a}}}));
 Json p={{"kind","portfolio"},{"title","P"},{"portfolioDescription",""},{"portfolioPages",Json::array()},{"permission","writers"},{"portfolioCursor",{{"runId","first"},{"previousRunId",""},{"frameId","f"},{"step",0},{"startedAt",0}}}};
 auto created=run(Json::array({{{"action","create"},{"id","p"},{"patch",p}}}));AGORA_CHECK(created.items["p"]["portfolioCursor"]["startedAt"].get<long long>()>1000000000000LL);
 auto cursor=p["portfolioCursor"];cursor["runId"]="second";cursor["step"]=1;
 bool stale=false;try{run(Json::array({{{"action","patch"},{"id","p"},{"patch",{{"portfolioCursor",cursor}}}}}));}catch(const std::invalid_argument& e){stale=std::string(e.what())=="PRESENTATION_STEP_STALE";}AGORA_CHECK(stale);
 cursor["previousRunId"]="first";run(Json::array({{{"action","patch"},{"id","p"},{"patch",{{"portfolioCursor",cursor}}}}}));
 a["frameId"]="missing";bool denied=false;try{run(Json::array({{{"action","patch"},{"id","a"},{"patch",a}}}));}catch(const std::invalid_argument& e){denied=std::string(e.what())=="FRAME_ACCESS_DENIED";}AGORA_CHECK(denied);
 a["frameId"]="f";a["animationPriority"]=-1;denied=false;try{agora::broker::validate(a);}catch(const std::invalid_argument&){denied=true;}AGORA_CHECK(denied);
}

int main(){return runTest("portfolio schema, frames, priority groups, cursor sequencing and server-stamped playback",[]{presentation();frames();});}
