#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <stdexcept>
namespace agora::textdelta {
using Json=nlohmann::json;
inline std::string hash(const std::string& value){std::uint64_t n=14695981039346656037ULL;for(unsigned char c:value){n^=c;n*=1099511628211ULL;}return std::to_string(n);}
inline bool boundary(const std::string& s,std::size_t i){return i==s.size()||(static_cast<unsigned char>(s[i])&0xc0)!=0x80;}
inline Json diff(const std::string& before,const std::string& after){
 std::size_t start=0,end=0;while(start<before.size()&&start<after.size()&&before[start]==after[start])++start;
 while(start&&!boundary(before,start))--start;
 while(end<before.size()-start&&end<after.size()-start&&before[before.size()-end-1]==after[after.size()-end-1])++end;
 while(end&&(!boundary(before,before.size()-end)||!boundary(after,after.size()-end)))--end;
 return Json{{"at",start},{"remove",before.size()-start-end},{"insert",after.substr(start,after.size()-start-end)},{"base",hash(before)}};
}
inline std::string apply(const std::string& before,const Json& edit){
 if(!edit.is_object()||edit.size()!=4||!edit.contains("at")||!edit["at"].is_number_unsigned()||!edit.contains("remove")||!edit["remove"].is_number_unsigned()||!edit.contains("insert")||!edit["insert"].is_string()||!edit.contains("base")||!edit["base"].is_string())throw std::invalid_argument("ITEM_TEXT_DELTA_INVALID");
 if(edit["base"].get<std::string>()!=hash(before))throw std::invalid_argument("CANVAS_TEXT_CONFLICT");
 const auto at=edit["at"].get<std::size_t>(),count=edit["remove"].get<std::size_t>();
 if(at>before.size()||count>before.size()-at||!boundary(before,at)||!boundary(before,at+count))throw std::invalid_argument("ITEM_TEXT_DELTA_INVALID");
 auto after=before;after.replace(at,count,edit["insert"].get<std::string>());return after;
}
}
