#include "service/CanvasBrokerOperations.hpp"
#include "TestSupport.hpp"
using Json=nlohmann::json;
void media(){Json item={{"kind","link"},{"mediaType","gif"},{"url","/api/canvases/1/media/11111111-1111-4111-8111-111111111111.gif"},{"mediaLoop",true},{"mediaState",{{"paused",true},{"time",0},{"updatedAt",1},{"actor","test"}}},{"x",0},{"y",0},{"width",.3},{"height",.3},{"permission","writers"}};agora::broker::validate(item);item["mediaLoop"]="true";bool rejected=false;try{agora::broker::validate(item);}catch(const std::invalid_argument&){rejected=true;}AGORA_CHECK(rejected);item["mediaLoop"]=false;auto r=agora::broker::prepare(Json::array({{{"action","create"},{"id","gif"},{"patch",item}}}),[](const std::string&)->std::optional<Json>{return std::nullopt;},{"writers"},false);AGORA_CHECK(r.items["gif"]["mediaState"]["paused"]==true);AGORA_CHECK(r.items["gif"]["mediaLoop"]==false);}
int main(){return runTest("uploaded media state and loop validation",media);}
