#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
namespace agora::agent {
using Json=nlohmann::json;
Json request(const Json& payload,const std::function<Json(const std::string&,const Json&)>& tool,const std::function<void(const std::string&)>& output);
}
