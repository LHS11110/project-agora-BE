#pragma once
#include "api/HttpServiceModule.hpp"
#include <httplib.h>
#include <memory>
#include <string>
#include <vector>

// HTTPS listener and common middleware. Endpoint definitions are services.
class HttpApiServer final {
public:
    HttpApiServer(const std::string& host, int port, std::vector<std::unique_ptr<HttpServiceModule>> services);
    ~HttpApiServer();
    void start();
    void stop();
private:
    void setupRoutes();
    std::vector<std::unique_ptr<HttpServiceModule>> api_modules_;
    std::string host_; int port_; std::string internal_api_token_;
    httplib::SSLServer server_;
};
