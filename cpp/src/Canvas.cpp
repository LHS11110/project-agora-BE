#include "Canvas.hpp"
#include <iostream>
#include <vector>

Canvas::Canvas(int canvas_id, const std::string& redis_ip, int redis_port)
    : canvas_id(canvas_id), redis_ip(redis_ip), redis_port(redis_port) {
}

Canvas::~Canvas() {
    disconnectAll();
}

std::pair<int, int> Canvas::connectUser(int user_id, int rx_port, int tx_port) {
    std::lock_guard<std::mutex> lock(canvas_mutex);

    // If user already connected, stop previous sockets only if new ports are provided
    if (rx_port > 0 && tx_port > 0) {
        if (user_sockets.find(user_id) != user_sockets.end()) {
            user_sockets[user_id]->stop();
        }
        auto sockets = std::make_shared<UserSockets>(user_id, rx_port, tx_port, this);
        sockets->start();
        user_sockets[user_id] = sockets;
    }

    user_conn_counts[user_id]++;
    active_users.insert(user_id);

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " connected (connections: " << user_conn_counts[user_id] << "). Total active users: " << active_users.size()
              << (rx_port > 0 ? " (RX: " + std::to_string(rx_port) + ", TX: " + std::to_string(tx_port) + ")" : " (WebSocket)") << "\n";

    return {rx_port, tx_port};
}

bool Canvas::disconnectUser(int user_id) {
    std::shared_ptr<UserSockets> socket_to_stop;
    std::size_t remaining_users = 0;
    bool user_became_inactive = false;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);

        const bool was_active = active_users.count(user_id) > 0;
        auto c_it = user_conn_counts.find(user_id);
        if (c_it != user_conn_counts.end()) {
            c_it->second--;
            if (c_it->second <= 0) {
                user_conn_counts.erase(c_it);
                active_users.erase(user_id);
                auto socket_it = user_sockets.find(user_id);
                if (socket_it != user_sockets.end()) {
                    socket_to_stop = socket_it->second;
                    user_sockets.erase(socket_it);
                }
            }
        } else {
            active_users.erase(user_id);
            auto socket_it = user_sockets.find(user_id);
            if (socket_it != user_sockets.end()) {
                socket_to_stop = socket_it->second;
                user_sockets.erase(socket_it);
            }
        }

        user_became_inactive = was_active && active_users.count(user_id) == 0;
        remaining_users = active_users.size();
    }

    if (socket_to_stop) {
        socket_to_stop->stop();
    }

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " disconnected. Remaining active users: " << remaining_users << "\n";
    return user_became_inactive;
}

void Canvas::disconnectUserCompletely(int user_id) {
    std::shared_ptr<UserSockets> socket_to_stop;
    WebSocketCallbacks callbacks;
    std::size_t remaining_users = 0;
    bool was_active = false;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        was_active = active_users.erase(user_id) > 0;
        user_conn_counts.erase(user_id);

        auto socket_it = user_sockets.find(user_id);
        if (socket_it != user_sockets.end()) {
            socket_to_stop = socket_it->second;
            user_sockets.erase(socket_it);
        }

        callbacks = web_socket_callbacks_;
        remaining_users = active_users.size();
    }

    if (socket_to_stop) {
        socket_to_stop->stop();
    }
    if ((was_active || socket_to_stop) && callbacks.disconnect_user) {
        callbacks.disconnect_user(canvas_id, user_id);
    }

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " fully disconnected. Remaining active users: " << remaining_users << "\n";
}

void Canvas::disconnectAll() {
    std::vector<std::shared_ptr<UserSockets>> sockets_to_stop;
    WebSocketCallbacks callbacks;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        for (auto& [uid, sock] : user_sockets) {
            if (sock) {
                sockets_to_stop.push_back(sock);
            }
        }
        user_sockets.clear();
        user_conn_counts.clear();
        active_users.clear();
        callbacks = web_socket_callbacks_;
    }

    for (const auto& socket : sockets_to_stop) {
        socket->stop();
    }
    if (callbacks.disconnect_all) {
        callbacks.disconnect_all(canvas_id);
    }

    std::cout << "[Canvas #" << canvas_id << "] All users disconnected\n";
}

void Canvas::setWebSocketCallbacks(WebSocketCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(canvas_mutex);
    web_socket_callbacks_ = std::move(callbacks);
}

void Canvas::broadcast(const nlohmann::json& data, int exclude_user_id) {
    std::vector<std::shared_ptr<UserSockets>> sockets;
    WebSocketCallbacks callbacks;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        for (auto& [uid, sock] : user_sockets) {
            if (uid != exclude_user_id && sock) {
                sockets.push_back(sock);
            }
        }
        callbacks = web_socket_callbacks_;
    }

    for (const auto& socket : sockets) {
        socket->sendJson(data);
    }
    if (callbacks.broadcast) {
        callbacks.broadcast(canvas_id, data, exclude_user_id);
    }
}

void Canvas::sendToUser(int user_id, const nlohmann::json& data) {
    std::shared_ptr<UserSockets> socket;
    WebSocketCallbacks callbacks;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        auto it = user_sockets.find(user_id);
        if (it != user_sockets.end()) {
            socket = it->second;
        }
        callbacks = web_socket_callbacks_;
    }

    if (socket) {
        socket->sendJson(data);
    }
    if (callbacks.send_to_user) {
        callbacks.send_to_user(canvas_id, user_id, data);
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
