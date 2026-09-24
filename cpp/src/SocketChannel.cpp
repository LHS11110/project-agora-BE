#include "SocketChannel.hpp"
#include "Canvas.hpp"
#include "RedisClient.hpp"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <initializer_list>
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

    // accept() must not block after a readiness notification becomes stale
    // while stop() is racing to shut down the listener.
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

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
    rx_server_fd_.store(createListeningSocket(rx_port_), std::memory_order_release);
    tx_server_fd_.store(createListeningSocket(tx_port_), std::memory_order_release);

    if (rx_server_fd_.load(std::memory_order_acquire) < 0
        || tx_server_fd_.load(std::memory_order_acquire) < 0) {
        std::cerr << "[UserSockets] Failed to bind listening socket on ports " << rx_port_ << "/" << tx_port_ << "\n";
    }

    rx_thread_ = std::thread(&UserSockets::rxLoop, this);
    tx_thread_ = std::thread(&UserSockets::txLoop, this);
}

void UserSockets::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(fd_mutex_);
        for (const int fd : {rx_client_fd_.load(std::memory_order_acquire),
                             tx_client_fd_.load(std::memory_order_acquire),
                             rx_server_fd_.load(std::memory_order_acquire),
                             tx_server_fd_.load(std::memory_order_acquire)}) {
            if (fd >= 0) shutdown(fd, SHUT_RDWR);
        }
    }

    if (rx_thread_.joinable()) rx_thread_.join();
    if (tx_thread_.joinable()) tx_thread_.join();

    // The loop threads own accepted-client cleanup. Once joined, close any
    // remaining descriptors and the listening sockets exactly once.
    std::lock_guard<std::mutex> lock(fd_mutex_);
    for (std::atomic<int>* descriptor : {&rx_client_fd_, &tx_client_fd_,
                                         &rx_server_fd_, &tx_server_fd_}) {
        const int fd = descriptor->exchange(-1, std::memory_order_acq_rel);
        if (fd >= 0) close(fd);
    }
}

bool UserSockets::sendJson(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    std::lock_guard<std::mutex> fd_lock(fd_mutex_);
    const int fd = rx_client_fd_.load(std::memory_order_acquire);
    if (fd < 0) {
        return false;
    }
    std::string text = data.dump() + "\n";
    ssize_t sent = write(fd, text.data(), text.length());
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
            } else if (item_obj.is_object() && item_obj.contains("permission")) {
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
                        const bool enabled = val.is_number_unsigned()
                            ? val.get<unsigned long long>() > 0
                            : val.is_number_integer() && val.get<long long>() > 0;
                        if (user_groups.count(grp_name) > 0 && enabled) {
                            accessible = true;
                            break;
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
        const int server_fd = rx_server_fd_.load(std::memory_order_acquire);
        if (server_fd < 0) break;

        struct pollfd pfd{};
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 200);
        if (ret <= 0 || !running_) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (!running_) break;
            usleep(50000);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            if (!running_) {
                close(client_fd);
                break;
            }
            rx_client_fd_.store(client_fd, std::memory_order_release);
        }

        std::cout << "[UserSockets] User #" << user_id_ << " connected to RX socket\n";

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
        while (running_) {
            const int current_client_fd = rx_client_fd_.load(std::memory_order_acquire);
            if (current_client_fd < 0) break;
            struct pollfd cpfd{};
            cpfd.fd = current_client_fd;
            cpfd.events = POLLIN;
            int cret = poll(&cpfd, 1, 200);
            if (cret < 0) break;
            if (cret == 0) continue; // timeout, check running_ again

            ssize_t r = recv(current_client_fd, buf, sizeof(buf), 0);
            if (r <= 0) {
                break;
            }
        }

        std::cout << "[UserSockets] User #" << user_id_ << " disconnected from RX socket\n";
        std::lock_guard<std::mutex> lock(fd_mutex_);
        const int current_client_fd = rx_client_fd_.exchange(-1, std::memory_order_acq_rel);
        if (current_client_fd >= 0) close(current_client_fd);
    }
}

void UserSockets::txLoop() {
    while (running_) {
        const int server_fd = tx_server_fd_.load(std::memory_order_acquire);
        if (server_fd < 0) break;

        struct pollfd pfd{};
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 200);
        if (ret <= 0 || !running_) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (!running_) break;
            usleep(50000);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            if (!running_) {
                close(client_fd);
                break;
            }
            tx_client_fd_.store(client_fd, std::memory_order_release);
        }

        std::cout << "[UserSockets] User #" << user_id_ << " connected to TX socket\n";

        char buf[4096];
        while (running_) {
            const int current_client_fd = tx_client_fd_.load(std::memory_order_acquire);
            if (current_client_fd < 0) break;
            struct pollfd cpfd{};
            cpfd.fd = current_client_fd;
            cpfd.events = POLLIN;
            int cret = poll(&cpfd, 1, 200);
            if (cret < 0) break;
            if (cret == 0) continue; // timeout, check running_ again

            ssize_t r = recv(current_client_fd, buf, sizeof(buf) - 1, 0);
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
        std::lock_guard<std::mutex> lock(fd_mutex_);
        const int current_client_fd = tx_client_fd_.exchange(-1, std::memory_order_acq_rel);
        if (current_client_fd >= 0) close(current_client_fd);
    }
}
