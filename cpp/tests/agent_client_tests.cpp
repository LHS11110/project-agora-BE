#include "service/AgentClient.hpp"
#include "service/RuntimeKernelClient.hpp"
#include "service/CanvasAgentTools.hpp"
#include "service/CanvasAgentSessions.hpp"
#include "TestSupport.hpp"
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
using Json=nlohmann::json;
void bridgeRoundTrip(){
 char directory[]="/tmp/agora-agent-test-XXXXXX";AGORA_CHECK(mkdtemp(directory));
 const std::string path=std::string(directory)+"/agent.sock";setenv("AGORA_AGENT_SOCKET",path.c_str(),1);
 int server=socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un address{};address.sun_family=AF_UNIX;std::strcpy(address.sun_path,path.c_str());
 AGORA_CHECK(bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);AGORA_CHECK(listen(server,1)==0);
 std::thread worker([&]{int fd=accept(server,nullptr,nullptr);FILE* io=fdopen(fd,"r+");char* line=nullptr;size_t size=0;
  getline(&line,&size,io);
  const std::string frames="{\"type\":\"output\",\"output\":\"reading\"}\n{\"type\":\"tool\",\"id\":1,\"name\":\"get_objects\",\"arguments\":{\"ids\":[\"a\"]}}\n";
  fwrite(frames.data(),1,frames.size(),io);fflush(io);getline(&line,&size,io);
  const auto result=Json::parse(line);const std::string final=Json{{"answer",result["result"]["text"]}}.dump()+"\n";fwrite(final.data(),1,final.size(),io);fflush(io);
  free(line);fclose(io);
 });
 std::string progress;auto result=agora::agent::request({{"message","hi"}},[](const std::string& name,const Json& args){AGORA_CHECK(name=="get_objects"&&args["ids"][0]=="a");return Json{{"text","done"}};},[&](const std::string& text){progress+=text;});
 worker.join();close(server);unlink(path.c_str());rmdir(directory);
 AGORA_CHECK(result["answer"]=="done"&&progress=="reading");
 auto unavailable=agora::agent::request(Json::object(),{},{});AGORA_CHECK(unavailable["error"]=="AGENT_UNAVAILABLE");
 Json session{{"status","complete"},{"owner_id",1},{"visibility",Json::array({Json::array({"a"}),Json::array({"b"})})}};
 AGORA_CHECK(!agora::agent::sessionVisible(session,2,{"a"},false));
 AGORA_CHECK(agora::agent::sessionVisible(session,2,{"a","b"},false));
 AGORA_CHECK(agora::agent::sessionVisible(session,2,{},true));
 session["status"]="draft";AGORA_CHECK(!agora::agent::sessionVisible(session,2,{"a","b"},true));
 AGORA_CHECK(agora::agent::sessionVisible(session,1,{"a","b"},false));
 AGORA_CHECK(!agora::agent::publicSession(session,2).contains("visibility"));
 agora::agent::options({{"reasoning_effort","high"},{"max_steps",3},{"max_tokens",4096}});
 for(const auto& invalid:Json::array({Json{{"max_steps",1.5}},Json{{"api_key","forged"}},Json{{"reasoning_effort","medium"}},Json{{"top_p",false}}})){
  bool rejected=false;try{agora::agent::options(invalid);}catch(const std::invalid_argument&){rejected=true;}AGORA_CHECK(rejected);
 }
 auto item=Json{{"kind","text"},{"renderMode","code"},{"text","print(1)"},{"x",.1},{"width",.4}};agora::agent::fields(item);
 item["permission"]="admin-group";bool denied=false;try{agora::agent::fields(item);}catch(const std::invalid_argument&){denied=true;}AGORA_CHECK(denied);
 auto publicItem=agora::agent::publicItem({{"kind","text"},{"text","source"},{"token","hidden"},{"runtimeResults",Json::array()}},true);AGORA_CHECK(publicItem["text"]=="source"&&!publicItem.contains("token")&&!publicItem.contains("runtimeResults"));

 // Agent options: API engine only; VM CLI agent options are rejected.
 agora::agent::options({{"engine","api"},{"reasoning_effort","low"},{"max_steps",6},{"locale","en"}});
 for(const auto& bad:Json::array({
     Json{{"engine","cli"}},
     Json{{"engine","local"}},
     Json{{"cli_tool","claude"}},
     Json{{"cli_command","agora-canvas list"}},
     Json{{"cli_model","x"}},
     Json{{"locale","de"}},
 })){
  bool rejected=false;try{agora::agent::options(bad);}catch(const std::invalid_argument&){rejected=true;}AGORA_CHECK(rejected);
 }

 // RuntimeKernelClient::requestWithTools bidirectional round-trip
 {
  char dir2[]="/tmp/agora-kernel-tool-test-XXXXXX";AGORA_CHECK(mkdtemp(dir2));
  const std::string kpath=std::string(dir2)+"/mgr.sock";
  int ksrv=socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un kaddr{};kaddr.sun_family=AF_UNIX;std::strcpy(kaddr.sun_path,kpath.c_str());
  AGORA_CHECK(bind(ksrv,reinterpret_cast<sockaddr*>(&kaddr),sizeof(kaddr))==0);AGORA_CHECK(listen(ksrv,1)==0);
  std::thread kworker([&]{
   int fd=accept(ksrv,nullptr,nullptr);FILE* io=fdopen(fd,"r+");char* line=nullptr;size_t size=0;
   getline(&line,&size,io);
   const auto req=Json::parse(line);AGORA_CHECK(req["action"]=="tool_stream");
   const std::string frames="{\"type\":\"output\",\"output\":\"starting\"}\n{\"type\":\"tool\",\"id\":7,\"name\":\"list_objects\",\"arguments\":{\"offset\":0}}\n";
   fwrite(frames.data(),1,frames.size(),io);fflush(io);
   getline(&line,&size,io);
   const auto tr=Json::parse(line);AGORA_CHECK(tr["type"]=="tool_result"&&tr["id"]==7&&tr["result"]["objects"][0]=="o1");
   const std::string done=Json{{"answer","done"}}.dump()+"\n";
   fwrite(done.data(),1,done.size(),io);fflush(io);
   free(line);fclose(io);
  });
  std::string kprog;
  auto kresult=RuntimeKernelClient(kpath).requestWithTools(
      {{"canvasId",1},{"action","tool_stream"}},
      [](const std::string& name,const Json& args){
       AGORA_CHECK(name=="list_objects"&&args["offset"]==0);
       return Json{{"objects",Json::array({"o1"})}};
      },
      [&](const std::string& text){kprog+=text;});
  kworker.join();close(ksrv);unlink(kpath.c_str());rmdir(dir2);
  AGORA_CHECK(kresult["answer"]=="done"&&kprog=="starting");
 }
}
int main(){return runTest("agent Unix bridge, tool validation and source filtering",bridgeRoundTrip);}
