#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <utility>

class RuntimeKernelClient {
public:
    explicit RuntimeKernelClient(std::string socket_path) : socket_path_(std::move(socket_path)) {}
    nlohmann::json request(const nlohmann::json& payload,
                           const std::function<void(const std::string&)>& on_output = {}) const;
    nlohmann::json requestWithTools(
        const nlohmann::json& payload,
        const std::function<nlohmann::json(const std::string&, const nlohmann::json&)>& on_tool,
        const std::function<void(const std::string&)>& on_output = {}) const;

private:
    std::string socket_path_;
};
