#pragma once

#include "CanvasPool.hpp"
#include "HttpApiModule.hpp"

class CanvasControlApi final : public HttpApiModule {
public:
    explicit CanvasControlApi(CanvasPool& canvas_pool) : canvas_pool_(canvas_pool) {}

    void registerRoutes(httplib::Server& server) override;

private:
    CanvasPool& canvas_pool_;
};
