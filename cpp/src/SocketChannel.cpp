#include "SocketChannel.hpp"
#include "Canvas.hpp"
#include "RedisClient.hpp"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <set>

UserSockets::UserSockets(int user_id, int rx_port, int tx_port, Canvas* canvas)
    : user_id_(user_id), rx_port_(rx_port), tx_port_(tx_port), canvas_(canvas) {
}

UserSockets::~UserSockets() {
    stop();
}

int UserSockets::createListeningSocket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void UserSockets::start() {
    running_ = true;
    rx_server_fd_ = createListeningSocket(rx_port_);
    tx_server_fd_ = createListeningSocket(tx_port_);

    if (rx_server_fd_ < 0 || tx_server_fd_ < 0) {
        std::cerr << "[UserSockets] Failed to bind listening socket on ports " << rx_port_ << "/" << tx_port_ << "\n";
    }

    rx_thread_ = std::thread(&UserSockets::rxLoop, this);
    tx_thread_ = std::thread(&UserSockets::txLoop, this);
}

void UserSockets::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (rx_client_fd_ >= 0) {
        shutdown(rx_client_fd_, SHUT_RDWR);
        close(rx_client_fd_);
        rx_client_fd_ = -1;
    }
    if (tx_client_fd_ >= 0) {
        shutdown(tx_client_fd_, SHUT_RDWR);
        close(tx_client_fd_);
        tx_client_fd_ = -1;
    }
    if (rx_server_fd_ >= 0) {
        shutdown(rx_server_fd_, SHUT_RDWR);
        close(rx_server_fd_);
        rx_server_fd_ = -1;
    }
    if (tx_server_fd_ >= 0) {
        shutdown(tx_server_fd_, SHUT_RDWR);
        close(tx_server_fd_);
        tx_server_fd_ = -1;
    }

    if (rx_thread_.joinable()) rx_thread_.join();
    if (tx_thread_.joinable()) tx_thread_.join();
}

bool UserSockets::sendJson(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (rx_client_fd_ < 0) {
        return false;
    }
    std::string text = data.dump() + "\n";
    ssize_t sent = write(rx_client_fd_, text.data(), text.length());
    return sent == (ssize_t)text.length();
}

void UserSockets::sendFilteredItems(const nlohmann::json& canvasDoc) {
    if (canvasDoc.is_null()) {
        return;
    }

    // 1. Identify which groups the user belongs to
    std::set<std::string> user_groups;
    bool is_admin = false;

    if (canvasDoc.contains("inner-group") && canvasDoc["inner-group"].is_object()) {
        for (auto& [group_name, members] : canvasDoc["inner-group"].items()) {
            if (members.is_array()) {
                for (auto& uid : members) {
                    if (uid.is_number_integer() && uid.get<int>() == user_id_) {
                        user_groups.insert(group_name);
                        if (group_name == "admin-group") {
                            is_admin = true;
                        }
                    }
                }
            }
        }
    }

    // 2. Filter items based on group permission
    nlohmann::json filtered_items = nlohmann::json::object();
    if (canvasDoc.contains("items") && canvasDoc["items"].is_object()) {
        for (auto& [item_id, item_obj] : canvasDoc["items"].items()) {
            bool accessible = false;

            if (is_admin) {
                accessible = true;
            } else if (item_obj.contains("permission")) {
                auto& perm = item_obj["permission"];
                if (perm.is_string()) {
                    accessible = (user_groups.count(perm.get<std::string>()) > 0);
                } else if (perm.is_array()) {
                    for (auto& p : perm) {
                        if (p.is_string() && user_groups.count(p.get<std::string>()) > 0) {
                            accessible = true;
                            break;
                        }
                    }
                } else if (perm.is_object()) {
                    for (auto& [grp_name, val] : perm.items()) {
                        if (user_groups.count(grp_name) > 0) {
                            if (val.is_number() && val.get<int>() > 0) {
                                accessible = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (accessible) {
                filtered_items[item_id] = item_obj;
            }
        }
    }

    nlohmann::json welcome_msg = {
        {"type", "init_items"},
        {"canvas_id", canvas_ ? canvas_->getCanvasId() : 0},
        {"user_id", user_id_},
        {"items", filtered_items}
    };

    sendJson(welcome_msg);
    std::cout << "[UserSockets] User #" << user_id_ << " sent " << filtered_items.size()
              << " filtered items on RX port " << rx_port_ << "\n";
}

void UserSockets::rxLoop() {
    while (running_) {
        if (rx_server_fd_ < 0) break;

        struct pollfd pfd{};
        pfd.fd = rx_server_fd_;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 200);
        if (ret <= 0 || !running_) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(rx_server_fd_, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (!running_) break;
            usleep(50000);
            continue;
        }

        std::cout << "[UserSockets] User #" << user_id_ << " connected to RX socket\n";
        rx_client_fd_ = client_fd;

        // Read cached canvas from Redis to send filtered items
        if (canvas_) {
            RedisClient redis(canvas_->getRedisIp(), canvas_->getRedisPort());
            auto cached_doc_str = redis.get("canvas:" + std::to_string(canvas_->getCanvasId()));
            if (cached_doc_str && !cached_doc_str->empty()) {
                try {
                    auto doc = nlohmann::json::parse(*cached_doc_str);
                    sendFilteredItems(doc);
                } catch (...) {
                }
            }
        }

        // Keep connection open; monitor for client close using poll
        char buf[128];
        while (running_ && rx_client_fd_ >= 0) {
            struct pollfd cpfd{};
            cpfd.fd = rx_client_fd_;
            cpfd.events = POLLIN;
            int cret = poll(&cpfd, 1, 200);
            if (cret < 0) break;
            if (cret == 0) continue; // timeout, check running_ again

            ssize_t r = recv(rx_client_fd_, buf, sizeof(buf), 0);
            if (r <= 0) {
                break;
            }
        }

        std::cout << "[UserSockets] User #" << user_id_ << " disconnected from RX socket\n";
        if (rx_client_fd_ >= 0) {
            close(rx_client_fd_);
            rx_client_fd_ = -1;
        }
    }
}

void UserSockets::txLoop() {
    while (running_) {
        if (tx_server_fd_ < 0) break;

        struct pollfd pfd{};
        pfd.fd = tx_server_fd_;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 200);
        if (ret <= 0 || !running_) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(tx_server_fd_, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (!running_) break;
            usleep(50000);
            continue;
        }

        std::cout << "[UserSockets] User #" << user_id_ << " connected to TX socket\n";
        tx_client_fd_ = client_fd;

        char buf[4096];
        while (running_ && tx_client_fd_ >= 0) {
            struct pollfd cpfd{};
            cpfd.fd = tx_client_fd_;
            cpfd.events = POLLIN;
            int cret = poll(&cpfd, 1, 200);
            if (cret < 0) break;
            if (cret == 0) continue; // timeout, check running_ again

            ssize_t r = recv(tx_client_fd_, buf, sizeof(buf) - 1, 0);
            if (r <= 0) {
                break;
            }
            buf[r] = '\0';
            // Echo / broadcast message if JSON
            try {
                auto msg = nlohmann::json::parse(buf);
                msg["sender_id"] = user_id_;
                if (canvas_) {
                    canvas_->broadcast(msg, user_id_);
                }
            } catch (...) {
            }
        }

        std::cout << "[UserSockets] User #" << user_id_ << " disconnected from TX socket\n";
        if (tx_client_fd_ >= 0) {
            close(tx_client_fd_);
            tx_client_fd_ = -1;
        }
    }
}
