#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <httplib.h>
#include "CanvasPool.hpp"

class HttpServer {
public:
    HttpServer(CanvasPool& canvas_pool, const std::string& host, int port, 
               const std::string& jwt_secret, const std::string& db_host, int db_port);
    ~HttpServer();

    void start();
    void stop();

    int authenticateTokenForCanvas(const std::string& token, int canvas_id, const std::string& client_ip, int ws_port);

    CanvasPool* getPool() { return &canvas_pool_; }

private:
    void setupRoutes();

    CanvasPool& canvas_pool_;
    std::string host_;
    int port_;
    std::string jwt_secret_;
    std::string db_host_;
    int db_port_;
    httplib::Server server_;
};
