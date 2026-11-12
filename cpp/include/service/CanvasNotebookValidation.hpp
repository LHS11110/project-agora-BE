#pragma once
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>
namespace agora::notebook {
inline void validate(const nlohmann::json& item){
 using Json=nlohmann::json;
 const auto invalid=[](){throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");};
 const auto role=item.value("runtimeRole","");
 const bool has_notebook_fields=item.contains("notebookCells")||item.contains("notebookResults")||item.contains("notebookCellIds")||item.contains("notebookId")||item.contains("notebookCell")||item.contains("notebookResult");
 if(!has_notebook_fields)return;
 if(item.value("kind","")!="shape")invalid();
 static const std::regex identifier("[a-zA-Z0-9_-]{1,128}");
 auto valid_cell=[&](const Json& cell){
  if(!cell.is_object()||cell.size()!=3||!cell.contains("id")||!cell["id"].is_string()||!std::regex_match(cell["id"].get<std::string>(),identifier)||!cell.contains("cellType")||!cell["cellType"].is_string()||(cell["cellType"]!="code"&&cell["cellType"]!="markdown")||!cell.contains("source")||!cell["source"].is_string()||cell["source"].get_ref<const std::string&>().size()>65536)return false;
  for(const auto& field:cell.items())if(field.key()!="id"&&field.key()!="cellType"&&field.key()!="source")return false;
  return true;
 };
 auto valid_result=[&](const Json& result){
  if(!result.is_object()||!result.contains("outputs")||!result["outputs"].is_array()||result["outputs"].size()>128||result.dump().size()>524288)return false;
  for(const auto& field:result.items())if(field.key()!="outputs"&&field.key()!="executionCount"&&field.key()!="exitCode"&&field.key()!="error"&&field.key()!="truncated")return false;
  if(result.contains("executionCount")&&!result["executionCount"].is_null()&&!result["executionCount"].is_number_integer())return false;
  if(result.contains("exitCode")&&!result["exitCode"].is_number_integer())return false;
  if(result.contains("error")&&!result["error"].is_string())return false;
  if(result.contains("truncated")&&!result["truncated"].is_boolean())return false;
  for(const auto& output:result["outputs"]){
   if(!output.is_object()||!output.contains("output_type")||!output["output_type"].is_string())return false;
   const auto type=output["output_type"].get<std::string>();
   if(type!="stream"&&type!="error"&&type!="display_data"&&type!="execute_result")return false;
   if(output.contains("text")&&!output["text"].is_string()&&!output["text"].is_array())return false;
   if(output.contains("data")){
    if(!output["data"].is_object())return false;
    for(const auto& mime:output["data"].items())if((mime.key()!="text/plain"&&mime.key()!="text/html"&&mime.key()!="text/markdown"&&mime.key()!="image/png"&&mime.key()!="image/jpeg"&&mime.key()!="image/svg+xml")||!mime.value().is_string())return false;
   }
  }
  return true;
 };
 if(role=="notebook"){
  if(item.contains("notebookId")||item.contains("notebookCell")||item.contains("notebookResult"))invalid();
  if(item.contains("notebookCellIds")){
   const auto& ids=item["notebookCellIds"];
   if(!ids.is_array()||ids.size()>32)invalid();
   std::unordered_set<std::string> seen;
   for(const auto& id:ids)if(!id.is_string()||!std::regex_match(id.get<std::string>(),identifier)||!seen.insert(id.get<std::string>()).second)invalid();
  }
  if(item.contains("notebookCells")){
   const auto& cells=item["notebookCells"];
   if(!cells.is_array()||cells.size()>32||cells.dump().size()>524288)invalid();
   std::unordered_set<std::string> ids;
   for(const auto& cell:cells)if(!valid_cell(cell)||!ids.insert(cell["id"].get<std::string>()).second)invalid();
  }
  if(item.contains("notebookResults")){
   const auto& results=item["notebookResults"];
   if(!results.is_object()||results.size()>32||results.dump().size()>1048576)invalid();
   for(const auto& pair:results.items())if(!std::regex_match(pair.key(),identifier)||!valid_result(pair.value()))invalid();
  }
  return;
 }
 if(role=="notebook-cell"){
  if(item.contains("notebookCells")||item.contains("notebookResults")||item.contains("notebookCellIds")||!item.contains("notebookId")||!item["notebookId"].is_string()||!std::regex_match(item["notebookId"].get<std::string>(),identifier)||!item.contains("notebookCell")||!valid_cell(item["notebookCell"]))invalid();
  if(item.contains("notebookResult")&&!valid_result(item["notebookResult"]))invalid();
  return;
 }
 invalid();
}
}
