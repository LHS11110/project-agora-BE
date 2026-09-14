#pragma once

#include <string>
#include <map>
#include <mutex>
#include <httplib.h>
#include "CanvasPool.hpp"

class HttpServer {
public:
    HttpServer(CanvasPool& canvas_pool, const std::string& host = "0.0.0.0", int port = 8000);
    ~HttpServer();

    void start();
    void stop();

    int authenticateToken(const std::string& token);

private:
    CanvasPool& canvas_pool_;
    std::string host_;
    int port_;
    httplib::Server server_;

    // JWT token registry: token -> user_id, and user_id -> token
    std::map<std::string, int> token_to_user_;
    std::map<int, std::string> user_to_token_;
    std::mutex auth_mutex_;

    void setupRoutes();

    bool registerToken(int user_id, const std::string& token);
};
