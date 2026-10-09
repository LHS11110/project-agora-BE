#pragma once

#include "api/HttpServiceModule.hpp"

class HealthService final : public HttpServiceModule {
public:
    void registerRoutes(HttpApi& api) override;
};
