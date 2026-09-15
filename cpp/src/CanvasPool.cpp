#include "CanvasPool.hpp"
#include <iostream>

CanvasPool::CanvasPool(const std::string& db_host, int db_port,
                       const std::string& es_host, int es_port,
                       const std::string& java_host, int java_port)
    : db_host_(db_host), db_port_(db_port), es_host_(es_host), es_port_(es_port),
      java_host_(java_host), java_port_(java_port) {
}

CanvasPool::~CanvasPool() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& [id, canvas] : canvases_) {
        if (canvas) {
            canvas->disconnectAll();
        }
    }
    canvases_.clear();
}

std::shared_ptr<Canvas> CanvasPool::getOrCreateCanvas(int canvas_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    auto it = canvases_.find(canvas_id);
    if (it != canvases_.end()) {
        return it->second;
    }

    std::cout << "[CanvasPool] Canvas #" << canvas_id << " not in pool. Initializing from MSSQL & ES...\n";

    // 1. Query MS SQL for assigned Redis IP & Port
    MssqlClient mssql(db_host_, db_port_);
    auto [redis_ip, redis_port] = mssql.getAssignedRedis(canvas_id);

    // 2. Query Elasticsearch for canvas document
    EsClient es(es_host_, es_port_);
    auto es_doc = es.getCanvasDocument(canvas_id);

    // 3. Cache into Redis
    RedisClient redis(redis_ip, redis_port);
    std::string canvas_name = "Canvas-" + std::to_string(canvas_id);
    int admin_uid = 0;

    if (es_doc.has_value()) {
        redis.set("canvas:" + std::to_string(canvas_id), es_doc->dump());
        std::cout << "[CanvasPool] Cached ES document for canvas #" << canvas_id
                  << " into Redis " << redis_ip << ":" << redis_port << "\n";

        if (es_doc->contains("canvas-name") && (*es_doc)["canvas-name"].is_string()) {
            canvas_name = (*es_doc)["canvas-name"].get<std::string>();
        }
        if (es_doc->contains("admin-user-id") && (*es_doc)["admin-user-id"].is_number_integer()) {
            admin_uid = (*es_doc)["admin-user-id"].get<int>();
        }
    } else {
        // Create minimal fallback JSON in Redis
        nlohmann::json fallback_json = {
            {"canvas-id", canvas_id},
            {"canvas-name", canvas_name},
            {"admin-user-id", admin_uid},
            {"description", ""},
            {"canvas-password-hash", nullptr},
            {"people", nlohmann::json::array()},
            {"inner-group", {{"admin-group", nlohmann::json::array()}}},
            {"items", nlohmann::json::object()},
            {"init-group", "default"}
        };
        redis.set("canvas:" + std::to_string(canvas_id), fallback_json.dump());
    }

    // 4. Create and register Canvas in pool
    auto canvas = std::make_shared<Canvas>(canvas_id, redis_ip, redis_port);
    canvas->setCanvasName(canvas_name);
    canvas->setAdminUserId(admin_uid);
    canvas->setWebSocketCallbacks(web_socket_callbacks_);

    canvases_[canvas_id] = canvas;
    std::cout << "[CanvasPool] Canvas #" << canvas_id << " successfully created and registered in pool\n";

    return canvas;
}

std::shared_ptr<Canvas> CanvasPool::getCanvas(int canvas_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = canvases_.find(canvas_id);
    if (it != canvases_.end()) {
        return it->second;
    }
    return nullptr;
}

void CanvasPool::setWebSocketCallbacks(Canvas::WebSocketCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    web_socket_callbacks_ = std::move(callbacks);
    for (auto& [id, canvas] : canvases_) {
        if (canvas) {
            canvas->setWebSocketCallbacks(web_socket_callbacks_);
        }
    }
}

void CanvasPool::unloadCanvas(int canvas_id, std::shared_ptr<Canvas> canvas) {
    if (!canvas) return;

    // 1. Disconnect all connected users
    canvas->disconnectAll();

    // 2. Fetch canvas document from Redis and reflect to Elasticsearch
    try {
        RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
        auto cached_str = redis.get("canvas:" + std::to_string(canvas_id));
        if (cached_str && !cached_str->empty()) {
            auto doc = nlohmann::json::parse(*cached_str);
            EsClient es(es_host_, es_port_);
            es.saveCanvasDocument(canvas_id, doc);
            std::cout << "[CanvasPool] Reflected Canvas #" << canvas_id << " from Redis to Elasticsearch\n";
        }

        // 3. Clean up Redis cache
        redis.del("canvas:" + std::to_string(canvas_id));
        redis.deletePattern("canvas:" + std::to_string(canvas_id) + ":*");
        std::cout << "[CanvasPool] Cleaned up Redis cache for Canvas #" << canvas_id << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[CanvasPool] Error during Redis/ES sync for canvas #" << canvas_id << ": " << e.what() << "\n";
    }

    // 4. Update MS SQL canvas_info: is_cached=false, redis/server ip&port=none(NULL)
    try {
        MssqlClient mssql(db_host_, db_port_);
        mssql.updateCanvasUncached(canvas_id);
    } catch (const std::exception& e) {
        std::cerr << "[CanvasPool] Error updating MS SQL for canvas #" << canvas_id << ": " << e.what() << "\n";
    }

    std::cout << "[CanvasPool] Canvas #" << canvas_id << " has no active users, unloaded from pool (load -1)\n";
}

bool CanvasPool::removeCanvas(int canvas_id) {
    std::shared_ptr<Canvas> canvas;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(canvas_id);
        if (it != canvases_.end()) {
            canvas = it->second;
            canvases_.erase(it);
        }
    }

    if (canvas) {
        unloadCanvas(canvas_id, canvas);
        return true;
    }
    return false;
}

void CanvasPool::disconnectUserFromAll(int user_id) {
    std::vector<std::pair<int, std::shared_ptr<Canvas>>> to_unload;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        for (auto it = canvases_.begin(); it != canvases_.end();) {
            if (it->second && it->second->isUserActive(user_id)) {
                it->second->disconnectUserCompletely(user_id);
                std::cout << "[CanvasPool] Disconnected user #" << user_id << " from Canvas #" << it->first << "\n";
                if (it->second->getActiveUsers().empty()) {
                    to_unload.push_back({it->first, it->second});
                    it = canvases_.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    for (auto& [id, canvas] : to_unload) {
        unloadCanvas(id, canvas);
    }
}

void CanvasPool::disconnectUser(int canvas_id, int user_id) {
    std::shared_ptr<Canvas> canvas_to_unload;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(canvas_id);
        if (it != canvases_.end() && it->second) {
            it->second->disconnectUserCompletely(user_id);
            std::cout << "[CanvasPool] Disconnected user #" << user_id << " from Canvas #" << canvas_id << "\n";
            if (it->second->getActiveUsers().empty()) {
                canvas_to_unload = it->second;
                canvases_.erase(it);
            }
        }
    }

    if (canvas_to_unload) {
        unloadCanvas(canvas_id, canvas_to_unload);
    }
}

void CanvasPool::disconnectWebSocketConnection(int canvas_id, int user_id) {
    std::shared_ptr<Canvas> canvas_to_unload;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(canvas_id);
        if (it != canvases_.end() && it->second) {
            it->second->disconnectUser(user_id);
            std::cout << "[CanvasPool] WebSocket connection closed for user #" << user_id
                      << " on Canvas #" << canvas_id << "\n";
            if (it->second->getActiveUsers().empty()) {
                canvas_to_unload = it->second;
                canvases_.erase(it);
            }
        }
    }

    if (canvas_to_unload) {
        unloadCanvas(canvas_id, canvas_to_unload);
    }
}

int CanvasPool::getActiveCanvasCount() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return (int)canvases_.size();
}

std::vector<int> CanvasPool::getActiveCanvasIds() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    std::vector<int> ids;
    ids.reserve(canvases_.size());
    for (const auto& [id, canvas] : canvases_) {
        ids.push_back(id);
    }
    return ids;
}

std::pair<int, int> CanvasPool::allocatePortPair() {
    int rx = next_port_.fetch_add(2);
    if (rx > 30000) {
        next_port_ = 9000;
        rx = 9000;
    }
    int tx = rx + 1;
    return {rx, tx};
}
