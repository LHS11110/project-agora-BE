#pragma once

#include "HttpApiModule.hpp"

class HealthApi final : public HttpApiModule {
public:
    void registerRoutes(httplib::Server& server) override;
};
