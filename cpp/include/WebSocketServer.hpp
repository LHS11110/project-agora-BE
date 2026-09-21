#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include "App.h"
#include "CanvasPool.hpp"

struct PerSocketData {
    int canvas_id{0};
    int user_id{0};
    int message_count{0};
    long long last_reset_time{0};
    bool is_admin{false};
    std::unordered_set<std::string> groups;
};

class WebSocketServer {
public:
    using Socket = uWS::WebSocket<false, true, PerSocketData>;
    using TokenValidator = std::function<int(const std::string&, int, const std::string&)>;

    WebSocketServer(CanvasPool& pool, const std::string& host = "0.0.0.0", int ws_port = 8001,
                    TokenValidator validator = nullptr,
                    const std::string& java_host = "127.0.0.1", int java_port = 8080);
    ~WebSocketServer();

    void start();
    void stop();

    int getWsPort() const { return ws_port_; }
    bool isRunning() const { return running_; }

    void broadcastToCanvas(int canvas_id, const nlohmann::json& data, int exclude_user_id = -1);
    void sendToUser(int canvas_id, int user_id, const nlohmann::json& data);
    void disconnectUser(int canvas_id, int user_id);
    void disconnectCanvas(int canvas_id);

private:
    void runServer();
    void registerSocket(Socket* ws);
    void unregisterSocket(Socket* ws);
    void beginWorker();
    void endWorker();

    CanvasPool& pool_;
    std::string host_;
    int ws_port_;
    TokenValidator token_validator_;
    std::string java_host_;
    int java_port_;
    std::thread ws_thread_;
    std::atomic<bool> running_{false};
    void* listen_socket_{nullptr};
    uWS::Loop* loop_{nullptr};
    std::mutex loop_mutex_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    int active_workers_{0};
    std::unordered_map<int, std::unordered_set<Socket*>> sockets_by_canvas_;
    std::unordered_map<int, std::unordered_map<std::string, std::unordered_set<std::string>>> item_permissions_by_canvas_;
};
