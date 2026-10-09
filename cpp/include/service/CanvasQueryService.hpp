#pragma once
#include "service_memory/CanvasLifecycleMemory.hpp"
class CanvasQueryService final {
public:
    CanvasQueryService(CanvasLifecycleMemory& pool, int logical_ws_port)
        : pool_(pool), logical_ws_port_(logical_ws_port) {}
    nlohmann::json execute(const nlohmann::json& input);
private:
    nlohmann::json join(const nlohmann::json& input);
    nlohmann::json query(const nlohmann::json& input);
    CanvasLifecycleMemory& pool_;
    int logical_ws_port_;
};
