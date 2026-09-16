#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <httplib.h>
#include "CanvasPool.hpp"

class HttpServer {
public:
    HttpServer(CanvasPool& canvas_pool, const std::string& host = "0.0.0.0", int port = 8000);
    ~HttpServer();

    void start();
    void stop();

    CanvasPool* getPool() { return &canvas_pool_; }

    int authenticateToken(const std::string& token);
    int authenticateTokenForCanvas(const std::string& token, int canvas_id);

private:
    CanvasPool& canvas_pool_;
    std::string host_;
    int port_;
    httplib::Server server_;

    struct TokenRegistration {
        int user_id{0};
        std::unordered_set<int> canvas_ids;
    };

    // JWT token registry: a token is only valid for canvases allocated through Spring.
    std::unordered_map<std::string, TokenRegistration> token_to_user_;
    std::mutex auth_mutex_;

    void setupRoutes();

    bool registerToken(int user_id, const std::string& token, int canvas_id);
};
