#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include "Canvas.hpp"
#include "RedisClient.hpp"
#include "EsClient.hpp"
#include "MssqlClient.hpp"

class CanvasPool {
public:
    CanvasPool(const std::string& db_host = "127.0.0.1", int db_port = 1433,
               const std::string& es_host = "127.0.0.1", int es_port = 9200);
    ~CanvasPool();

    // Select or create canvas in pool
    std::shared_ptr<Canvas> getOrCreateCanvas(int canvas_id);
    std::shared_ptr<Canvas> getCanvas(int canvas_id);

    // Remove canvas from pool, close sockets, clean up Redis
    bool removeCanvas(int canvas_id);

    // Disconnect a user across all active canvases
    void disconnectUserFromAll(int user_id);

    int getActiveCanvasCount();

    // Port allocation for RX and TX sockets
    std::pair<int, int> allocatePortPair();

private:
    std::unordered_map<int, std::shared_ptr<Canvas>> canvases_;
    std::mutex pool_mutex_;

    std::string db_host_;
    int db_port_;
    std::string es_host_;
    int es_port_;

    std::atomic<int> next_port_{9000};
};
