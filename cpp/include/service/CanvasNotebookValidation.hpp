#pragma once
#include <nlohmann/json.hpp>
#include <regex>
#include <unordered_set>
namespace agora::notebook {
inline void validate(const nlohmann::json& item){
 using Json=nlohmann::json;
 const auto invalid=[](){throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");};
 if(!item.contains("notebookCells")&&!item.contains("notebookResults"))return;
 if(item.value("kind","")!="shape"||item.value("runtimeRole","")!="notebook")invalid();
 static const std::regex identifier("[a-zA-Z0-9_-]{1,128}");
 if(item.contains("notebookCells")){
  const auto& cells=item["notebookCells"];
  if(!cells.is_array()||cells.size()>32||cells.dump().size()>524288)invalid();
  std::unordered_set<std::string> ids;
  for(const auto& cell:cells){
   if(!cell.is_object()||cell.size()!=3||!cell.contains("id")||!cell["id"].is_string()||!std::regex_match(cell["id"].get<std::string>(),identifier)||!ids.insert(cell["id"].get<std::string>()).second||!cell.contains("cellType")||!cell["cellType"].is_string()||(cell["cellType"]!="code"&&cell["cellType"]!="markdown")||!cell.contains("source")||!cell["source"].is_string()||cell["source"].get_ref<const std::string&>().size()>65536)invalid();
  }
 }
 if(item.contains("notebookResults")){
  const auto& results=item["notebookResults"];
  if(!results.is_object()||results.size()>32||results.dump().size()>1048576)invalid();
  for(const auto& pair:results.items()){
   const auto& result=pair.value();
   if(!std::regex_match(pair.key(),identifier)||!result.is_object()||!result.contains("outputs")||!result["outputs"].is_array()||result["outputs"].size()>128)invalid();
   for(const auto& field:result.items())if(field.key()!="outputs"&&field.key()!="executionCount"&&field.key()!="exitCode"&&field.key()!="error"&&field.key()!="truncated")invalid();
   if(result.contains("executionCount")&&!result["executionCount"].is_null()&&!result["executionCount"].is_number_integer())invalid();
   if(result.contains("exitCode")&&!result["exitCode"].is_number_integer())invalid();
   if(result.contains("error")&&!result["error"].is_string())invalid();
   if(result.contains("truncated")&&!result["truncated"].is_boolean())invalid();
   for(const auto& output:result["outputs"]){
    if(!output.is_object()||!output.contains("output_type")||!output["output_type"].is_string())invalid();
    const auto type=output["output_type"].get<std::string>();
    if(type!="stream"&&type!="error"&&type!="display_data"&&type!="execute_result")invalid();
    if(output.contains("text")&&!output["text"].is_string()&&!output["text"].is_array())invalid();
    if(output.contains("data")){
     if(!output["data"].is_object())invalid();
     for(const auto& mime:output["data"].items())if((mime.key()!="text/plain"&&mime.key()!="text/html"&&mime.key()!="image/png"&&mime.key()!="image/jpeg"&&mime.key()!="image/svg+xml")||!mime.value().is_string())invalid();
    }
   }
  }
 }
}
}
