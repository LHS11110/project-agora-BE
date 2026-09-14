#include "Canvas.hpp"
#include <iostream>

Canvas::Canvas(int canvas_id, const std::string& redis_ip, int redis_port)
    : canvas_id(canvas_id), redis_ip(redis_ip), redis_port(redis_port) {
}

Canvas::~Canvas() {
    disconnectAll();
}

std::pair<int, int> Canvas::connectUser(int user_id, int rx_port, int tx_port) {
    std::lock_guard<std::mutex> lock(canvas_mutex);

    // If user already connected, stop previous sockets
    if (user_sockets.find(user_id) != user_sockets.end()) {
        user_sockets[user_id]->stop();
    }

    auto sockets = std::make_shared<UserSockets>(user_id, rx_port, tx_port, this);
    sockets->start();

    user_sockets[user_id] = sockets;
    active_users.insert(user_id);

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " connected. Total active users: " << active_users.size()
              << " (RX: " << rx_port << ", TX: " << tx_port << ")\n";

    return {rx_port, tx_port};
}

void Canvas::disconnectUser(int user_id) {
    std::lock_guard<std::mutex> lock(canvas_mutex);

    auto it = user_sockets.find(user_id);
    if (it != user_sockets.end()) {
        it->second->stop();
        user_sockets.erase(it);
    }
    active_users.erase(user_id);

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " disconnected. Remaining active users: " << active_users.size() << "\n";
}

void Canvas::disconnectAll() {
    std::lock_guard<std::mutex> lock(canvas_mutex);

    for (auto& [uid, sock] : user_sockets) {
        if (sock) {
            sock->stop();
        }
    }
    user_sockets.clear();
    active_users.clear();

    std::cout << "[Canvas #" << canvas_id << "] All users disconnected\n";
}

void Canvas::broadcast(const nlohmann::json& data, int exclude_user_id) {
    std::lock_guard<std::mutex> lock(canvas_mutex);

    for (auto& [uid, sock] : user_sockets) {
        if (uid != exclude_user_id && sock) {
            sock->sendJson(data);
        }
    }
}

void Canvas::sendToUser(int user_id, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(canvas_mutex);

    auto it = user_sockets.find(user_id);
    if (it != user_sockets.end() && it->second) {
        it->second->sendJson(data);
    }
}

bool Canvas::isUserActive(int user_id) {
    std::lock_guard<std::mutex> lock(canvas_mutex);
    return active_users.count(user_id) > 0;
}

std::set<int> Canvas::getActiveUsers() {
    std::lock_guard<std::mutex> lock(canvas_mutex);
    return active_users;
}
