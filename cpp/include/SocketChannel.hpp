#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>

class Canvas; // Forward declaration

class UserSockets {
public:
    UserSockets(int user_id, int rx_port, int tx_port, Canvas* canvas);
    ~UserSockets();

    int getUserId() const { return user_id_; }
    int getRxPort() const { return rx_port_; }
    int getTxPort() const { return tx_port_; }

    void start();
    void stop();
    bool sendJson(const nlohmann::json& data);

    // Initial items push
    void sendFilteredItems(const nlohmann::json& canvasDoc);

private:
    int user_id_;
    int rx_port_;
    int tx_port_;
    Canvas* canvas_;

    int rx_server_fd_{-1};
    int tx_server_fd_{-1};
    int rx_client_fd_{-1};
    int tx_client_fd_{-1};

    std::atomic<bool> running_{false};
    std::thread rx_thread_;
    std::thread tx_thread_;
    std::mutex send_mutex_;

    void rxLoop();
    void txLoop();
    static int createListeningSocket(int port);
};
