#pragma once

#include <unordered_map>
#include <vector>
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
               const std::string& es_host = "127.0.0.1", int es_port = 9200,
               const std::string& java_host = "127.0.0.1", int java_port = 8080);
    ~CanvasPool();

    // Select or create canvas in pool
    std::shared_ptr<Canvas> getOrCreateCanvas(int canvas_id);
    std::shared_ptr<Canvas> getCanvas(int canvas_id);

    // Remove canvas from pool, close sockets, clean up Redis, reflect to ES, update MSSQL
    bool removeCanvas(int canvas_id);

    // Disconnect a user across all active canvases
    void disconnectUserFromAll(int user_id);

    // Disconnect a user from a specific canvas (unloads canvas if no active users remain)
    void disconnectUser(int canvas_id, int user_id);

    // Record one WebSocket connection closing without terminating a user's other connections.
    void disconnectWebSocketConnection(int canvas_id, int user_id);

    // Configure WebSocket delivery for existing and future canvases.
    void setWebSocketCallbacks(Canvas::WebSocketCallbacks callbacks);

    int getActiveCanvasCount();
    std::vector<int> getActiveCanvasIds();

    // Port allocation for RX and TX sockets
    std::pair<int, int> allocatePortPair();

    std::string getJavaHost() const { return java_host_; }
    int getJavaPort() const { return java_port_; }

    std::string getDbHost() const { return db_host_; }
    int getDbPort() const { return db_port_; }

private:
    void unloadCanvas(int canvas_id, std::shared_ptr<Canvas> canvas);

    std::unordered_map<int, std::shared_ptr<Canvas>> canvases_;
    std::mutex pool_mutex_;
    Canvas::WebSocketCallbacks web_socket_callbacks_;

    std::string db_host_;
    int db_port_;
    std::string es_host_;
    int es_port_;
    std::string java_host_;
    int java_port_;

    std::atomic<int> next_port_{9000};
};
