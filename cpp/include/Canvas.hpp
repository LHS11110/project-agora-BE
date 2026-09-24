#pragma once

#include <string>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <deque>
#include <nlohmann/json.hpp>
#include "SocketChannel.hpp"

class Canvas {
public:
    struct WebSocketCallbacks {
        std::function<void(int, const nlohmann::json&, int)> broadcast;
        std::function<void(int, int, const nlohmann::json&)> send_to_user;
        std::function<void(int, int)> disconnect_user;
        std::function<void(int)> disconnect_all;
    };

    Canvas(int canvas_id, const std::string& redis_ip = "127.0.0.1", int redis_port = 6379);
    ~Canvas();

    int getCanvasId() const { return canvas_id; }
    std::string getCanvasName() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return canvas_name;
    }
    void setCanvasName(const std::string& name) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        canvas_name = name;
    }
    long long getSettingsRevision() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return settings_revision_;
    }
    void setSettingsRevision(long long revision) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        settings_revision_ = revision;
    }
    int getAdminUserId() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return admin_user_id;
    }
    void setAdminUserId(int id) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        admin_user_id = id;
    }

    std::string getRedisIp() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return redis_ip;
    }
    int getRedisPort() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return redis_port;
    }
    void setRedisConfig(const std::string& ip, int port) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        redis_ip = ip;
        redis_port = port;
    }

    // Connect user: allocates sockets, adds to active_users set, maps to user_sockets map
    std::pair<int, int> connectUser(int user_id, int rx_port, int tx_port);

    // Disconnect one connection for a user. Returns true when the user became inactive.
    bool disconnectUser(int user_id);

    // Disconnect every transport connection for a user.
    void disconnectUserCompletely(int user_id);

    // Disconnect all users
    void disconnectAll();

    // WebSocket transport callbacks are supplied by WebSocketServer through CanvasPool.
    void setWebSocketCallbacks(WebSocketCallbacks callbacks);

    // Broadcast message to all active users on their RX sockets and WebSocket sessions.
    void broadcast(const nlohmann::json& data, int exclude_user_id = -1);

    // Send message to specific user on every active transport.
    void sendToUser(int user_id, const nlohmann::json& data);

    bool isUserActive(int user_id);
    std::set<int> getActiveUsers();

    // Data structures explicitly required:
    int canvas_id;
    std::string canvas_name;
    int admin_user_id{0};
    std::string redis_ip;
    int redis_port;
    std::set<int> active_users;
    std::map<int, int> user_conn_counts;
    std::map<int, std::shared_ptr<UserSockets>> user_sockets;

    // Serializes settings mutations with the final Redis -> Elasticsearch flush.
    std::mutex settings_mutex;
    std::atomic<bool> unloading{false};

    // Persistence queue bookkeeping has a separate short-held mutex so event
    // loop enqueue never waits on settings/Redis/SQL/ES work. Tickets let
    // readers wait only for writes already accepted before their request.
    // Unload marks the canvas as closing and waits for all reserved Redis
    // writes before taking the final Redis -> Elasticsearch snapshot.
    bool enqueuePersistence(const nlohmann::json& event, bool& start_worker);
    std::uint64_t persistenceBarrier();
    bool nextPersistence(nlohmann::json& event, std::uint64_t& ticket);
    void cancelPersistenceQueue();
    void endPersistence(std::uint64_t ticket);
    void waitForPersistenceThrough(std::uint64_t ticket);
    void waitForPendingPersistence(std::unique_lock<std::mutex>& lock);

private:
    mutable std::mutex metadata_mutex_;
    long long settings_revision_{0};
    std::mutex canvas_mutex;
    std::mutex persistence_mutex_;
    std::condition_variable persistence_cv_;
    std::size_t pending_persistence_{0};
    std::uint64_t last_enqueued_persistence_{0};
    std::uint64_t last_completed_persistence_{0};
    std::deque<std::pair<std::uint64_t, nlohmann::json>> persistence_queue_;
    bool persistence_worker_running_{false};
    WebSocketCallbacks web_socket_callbacks_;
};
