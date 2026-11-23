#pragma once
#include "service_memory/CanvasLifecycleMemory.hpp"
#include <functional>
#include <string>
class CanvasQueryService final {
public:
    using OutputHandler = std::function<void(const std::string&)>;
    CanvasQueryService(CanvasLifecycleMemory& pool, int logical_ws_port,
                       std::string runtime_socket = "/run/agora-runtime/manager.sock")
        : pool_(pool), logical_ws_port_(logical_ws_port),
          runtime_socket_(runtime_socket.empty() ? "/run/agora-runtime/manager.sock" : std::move(runtime_socket)) {}
    nlohmann::json execute(const nlohmann::json& input);
    nlohmann::json execute(const nlohmann::json& input, const OutputHandler& on_output);
private:
    nlohmann::json executeInternal(const nlohmann::json& input, const OutputHandler& on_output, bool agent_tool);
    nlohmann::json join(const nlohmann::json& input);
    nlohmann::json query(const nlohmann::json& input, const OutputHandler& on_output = {}, bool agent_creation = false, bool agent_tool = false);
    CanvasLifecycleMemory& pool_;
    int logical_ws_port_;
    std::string runtime_socket_;
};
