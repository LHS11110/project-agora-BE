#include "service/AgentClient.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
namespace agora::agent {
Json request(const Json& payload,const std::function<Json(const std::string&,const Json&)>& tool,const std::function<void(const std::string&)>& output){
 const char* configured=std::getenv("AGORA_AGENT_SOCKET");const std::string path=configured?configured:"/run/agora-agent/agent.sock";
 sockaddr_un address{};if(path.size()>=sizeof(address.sun_path))throw std::runtime_error("AGENT_UNAVAILABLE");
 const int fd=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)throw std::runtime_error("AGENT_UNAVAILABLE");
 struct Guard{int fd;~Guard(){::shutdown(fd,SHUT_RDWR);::close(fd);}}guard{fd};
 timeval timeout{225,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));timeval sending{10,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&sending,sizeof(sending));
 address.sun_family=AF_UNIX;std::memcpy(address.sun_path,path.c_str(),path.size()+1);
 if(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0)return {{"error","AGENT_UNAVAILABLE"},{"message","에이전트 서버에 연결할 수 없습니다."}};
 const auto send=[&](const Json& value){const auto text=value.dump()+"\n";if(text.size()>1000000)throw std::runtime_error("AGENT_FRAME_TOO_LARGE");std::size_t at=0;while(at<text.size()){const auto n=::send(fd,text.data()+at,text.size()-at,MSG_NOSIGNAL);if(n<=0)throw std::runtime_error("AGENT_UNAVAILABLE");at+=n;}};
 send(payload);std::string pending;char data[8192];int calls=0;
 for(;;){
  auto newline=pending.find('\n');if(newline==std::string::npos){const auto n=::recv(fd,data,sizeof(data),0);if(n<=0)throw std::runtime_error("AGENT_UNAVAILABLE");pending.append(data,n);if(pending.size()>1000000)throw std::runtime_error("AGENT_FRAME_TOO_LARGE");continue;}
  const auto frame=Json::parse(pending.substr(0,newline));pending.erase(0,newline+1);
  if(frame.value("type","")=="output"){if(output)output(frame.value("output",std::string()).substr(0,4000));continue;}
  if(frame.value("type","")=="tool"){
   if(++calls>80)throw std::runtime_error("AGENT_TOOL_LIMIT");Json result;
   try{result=tool(frame.at("name").get<std::string>(),frame.at("arguments"));}catch(const std::exception&){result={{"error","AGENT_TOOL_REJECTED"},{"message","도구 요청이 거부되었습니다."}};}
   send({{"type","tool_result"},{"id",frame.at("id")},{"result",result}});continue;
  }
  return frame;
 }
}
}
