#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
namespace agora::question {
using Json=nlohmann::json;
// Match JavaScript string.length / textarea maxlength for valid UTF-8 JSON.
inline std::size_t utf16Length(const std::string& value){std::size_t count=0;for(unsigned char ch:value)if((ch&0xc0)!=0x80)count+=ch>=0xf0?2:1;return count;}
inline void validate(const Json& item){
 if(item.value("kind","")!="question")return;
 auto text=[&](const char* key,std::size_t max){return item.contains(key)&&item[key].is_string()&&utf16Length(item[key].get_ref<const std::string&>())<=max;};
 if(!text("questionType",32)||(item["questionType"]!="choice"&&item["questionType"]!="short-answer")||!text("questionPrompt",4000)||!text("questionAnswer",8000)||!text("questionFeedback",2000)||!text("questionVersion",128)||item["questionVersion"].get_ref<const std::string&>().empty()||!text("questionGradedVersion",128))throw std::invalid_argument("QUESTION_DATA_INVALID");
 if(!item.contains("questionOptions")||!item["questionOptions"].is_array()||item["questionOptions"].size()<2||item["questionOptions"].size()>10)throw std::invalid_argument("QUESTION_OPTIONS_INVALID");
 for(const auto& option:item["questionOptions"])if(!option.is_string()||utf16Length(option.get_ref<const std::string&>())>300)throw std::invalid_argument("QUESTION_OPTIONS_INVALID");
 if(!item.contains("questionSubmitted")||!item["questionSubmitted"].is_boolean()||!item.contains("questionMaxScore")||!item["questionMaxScore"].is_number_integer()||item["questionMaxScore"].get<double>()<1||item["questionMaxScore"].get<double>()>1000||!item.contains("questionScore")||!item["questionScore"].is_number())throw std::invalid_argument("QUESTION_SCORE_INVALID");
 const auto score=item["questionScore"].get<double>();if(!std::isfinite(score)||(score!=-1&&(score<0||score>item["questionMaxScore"].get<double>())))throw std::invalid_argument("QUESTION_SCORE_INVALID");
 const auto& answer=item["questionAnswer"].get_ref<const std::string&>();
 if(item["questionType"]=="choice"&&!answer.empty()){
  if(answer.size()!=1||answer[0]<'0'||answer[0]>'9'||std::size_t(answer[0]-'0')>=item["questionOptions"].size())throw std::invalid_argument("QUESTION_ANSWER_INVALID");
 }
 if(item["questionSubmitted"].get<bool>()&&(answer.find_first_not_of(" \n\r\t")==std::string::npos||item["questionPrompt"].get_ref<const std::string&>().find_first_not_of(" \n\r\t")==std::string::npos))throw std::invalid_argument("QUESTION_ANSWER_INVALID");
 if(item["questionSubmitted"].get<bool>()&&item["questionType"]=="choice")for(const auto& option:item["questionOptions"])if(option.get_ref<const std::string&>().find_first_not_of(" \n\r\t")==std::string::npos)throw std::invalid_argument("QUESTION_OPTIONS_INVALID");
 if(score>=0&&(!item["questionSubmitted"].get<bool>()||item["questionVersion"]!=item["questionGradedVersion"]))throw std::invalid_argument("QUESTION_GRADING_STALE");
}
inline void validateRevision(const Json& old,const Json& item){
 if(!old.is_object()||old.value("kind","")!="question"||item.value("kind","")!="question")return;
 for(const auto* key:{"questionType","questionPrompt","questionOptions","questionAnswer","questionSubmitted","questionMaxScore"})
  if(old.value(key,Json())!=item.value(key,Json())&&old.value("questionVersion",Json())==item.value("questionVersion",Json()))throw std::invalid_argument("QUESTION_REVISION_REQUIRED");
}
}
