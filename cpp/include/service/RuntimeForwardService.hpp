#pragma once

#include "api/HttpServiceModule.hpp"
#include "service_memory/CanvasLifecycleMemory.hpp"
#include <string>

class RuntimeForwardService final : public HttpServiceModule {
public:
    RuntimeForwardService(CanvasLifecycleMemory& canvases, std::string manager_socket)
        : canvases_(canvases), manager_socket_(std::move(manager_socket)) {}

    void registerRoutes(HttpApi& api) override;

private:
    CanvasLifecycleMemory& canvases_;
    std::string manager_socket_;
};
