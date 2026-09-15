#pragma once

#include <string>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include "SocketChannel.hpp"

class Canvas {
public:
    Canvas(int canvas_id, const std::string& redis_ip = "127.0.0.1", int redis_port = 6379);
    ~Canvas();

    int getCanvasId() const { return canvas_id; }
    std::string getCanvasName() const { return canvas_name; }
    void setCanvasName(const std::string& name) { canvas_name = name; }
    int getAdminUserId() const { return admin_user_id; }
    void setAdminUserId(int id) { admin_user_id = id; }

    std::string getRedisIp() const { return redis_ip; }
    int getRedisPort() const { return redis_port; }
    void setRedisConfig(const std::string& ip, int port) {
        redis_ip = ip;
        redis_port = port;
    }

    // Connect user: allocates sockets, adds to active_users set, maps to user_sockets map
    std::pair<int, int> connectUser(int user_id, int rx_port, int tx_port);

    // Disconnect user: removes from active_users and user_sockets
    void disconnectUser(int user_id);

    // Disconnect all users
    void disconnectAll();

    // Broadcast message to all active users on their RX sockets
    void broadcast(const nlohmann::json& data, int exclude_user_id = -1);

    // Send message to specific user
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

private:
    std::mutex canvas_mutex;
};
