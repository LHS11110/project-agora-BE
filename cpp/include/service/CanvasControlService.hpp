#pragma once

#include "service_memory/CanvasLifecycleMemory.hpp"
#include "api/HttpServiceModule.hpp"

class CanvasControlService final : public HttpServiceModule {
public:
    explicit CanvasControlService(CanvasLifecycleMemory& canvas_pool) : canvas_pool_(canvas_pool) {}

    void registerRoutes(HttpApi& api) override;

private:
    CanvasLifecycleMemory& canvas_pool_;
};
