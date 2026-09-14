#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include "CanvasPool.hpp"

struct PerSocketData {
    int canvas_id{0};
    int user_id{0};
};

class WebSocketServer {
public:
    using TokenValidator = std::function<int(const std::string&)>;

    WebSocketServer(CanvasPool& pool, const std::string& host = "0.0.0.0", int ws_port = 8001,
                    TokenValidator validator = nullptr);
    ~WebSocketServer();

    void start();
    void stop();

    int getWsPort() const { return ws_port_; }
    bool isRunning() const { return running_; }

private:
    void runServer();

    CanvasPool& pool_;
    std::string host_;
    int ws_port_;
    TokenValidator token_validator_;
    std::thread ws_thread_;
    std::atomic<bool> running_{false};
    void* listen_socket_{nullptr};
    void* loop_{nullptr};
};
