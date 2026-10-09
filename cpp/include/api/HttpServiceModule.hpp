#pragma once

#include "api/HttpApi.hpp"

/** A separately registerable service definition of HTTP endpoints. */
class HttpServiceModule {
public:
    virtual ~HttpServiceModule() = default;
    virtual void registerRoutes(HttpApi& api) = 0;
};
