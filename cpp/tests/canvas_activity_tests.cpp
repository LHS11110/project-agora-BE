#include "service/CanvasActivity.hpp"
#include "TestSupport.hpp"
using Json=nlohmann::json;
void testActivity(){
 Json old={{"a",{{"kind","text"},{"text","one"},{"runtimeState",{{"status","running"}}}}}};
 auto same=old;same["a"]["runtimeState"]["status"]="done";auto zero=agora::activity::delta(old,same);AGORA_CHECK(zero["updated"]==0);
 auto edited=old;edited["a"]["text"]="two";AGORA_CHECK(agora::activity::delta(old,edited)["updated"]==1);
 AGORA_CHECK(agora::activity::delta(Json::object(),old)["created"]==1);AGORA_CHECK(agora::activity::delta(old,{{"a",nullptr}})["deleted"]==1);
 Json stroke={{"s",{{"kind","stroke"},{"complete",false}}}};AGORA_CHECK(agora::activity::delta(Json::object(),stroke)["created"]==0);
 auto finished=stroke;finished["s"]["complete"]=true;AGORA_CHECK(agora::activity::delta(stroke,finished)["created"]==1);
 AGORA_CHECK(agora::activity::delta(stroke,{{"s",nullptr}})["deleted"]==0);
 AGORA_CHECK(agora::activity::koreanDay().size()==10);
 using Clock=std::chrono::system_clock;
 const auto at=[](long long seconds){return Clock::time_point{std::chrono::seconds{seconds}};};
 AGORA_CHECK(agora::activity::nextFlushTime(at(60))==at(1800));
 AGORA_CHECK(agora::activity::nextFlushTime(at(1799))==at(1800));
 AGORA_CHECK(agora::activity::nextFlushTime(at(1800))==at(3600));
 AGORA_CHECK(agora::activity::nextFlushTime(at(1850))==at(3600));
}
int main(){return runTest("authoritative canvas object activity excludes automatic state and incomplete strokes",testActivity);}
