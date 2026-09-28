#pragma once

#include <httplib.h>

/** A separately registerable group of HTTP endpoints. */
class HttpApiModule {
public:
    virtual ~HttpApiModule() = default;
    virtual void registerRoutes(httplib::Server& server) = 0;
};
