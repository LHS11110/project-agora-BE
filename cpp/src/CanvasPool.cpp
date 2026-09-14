#include "CanvasPool.hpp"
#include <iostream>

CanvasPool::CanvasPool(const std::string& db_host, int db_port,
                       const std::string& es_host, int es_port)
    : db_host_(db_host), db_port_(db_port), es_host_(es_host), es_port_(es_port) {
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
        // Disconnect all connected users
        canvas->disconnectAll();

        // Clean up Redis
        RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
        redis.del("canvas:" + std::to_string(canvas_id));
        redis.deletePattern("canvas:" + std::to_string(canvas_id) + ":*");

        std::cout << "[CanvasPool] Canvas #" << canvas_id << " removed from pool and Redis cleaned up\n";
        return true;
    }
    return false;
}

void CanvasPool::disconnectUserFromAll(int user_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    std::vector<int> empty_canvases;
    for (auto& [id, canvas] : canvases_) {
        if (canvas && canvas->isUserActive(user_id)) {
            canvas->disconnectUser(user_id);
            std::cout << "[CanvasPool] Disconnected user #" << user_id << " from Canvas #" << id << "\n";
            if (canvas->getActiveUsers().empty()) {
                empty_canvases.push_back(id);
            }
        }
    }
    for (int id : empty_canvases) {
        canvases_.erase(id);
        std::cout << "[CanvasPool] Canvas #" << id << " has no active users, unloaded from pool (load -1)\n";
    }
}

void CanvasPool::disconnectUser(int canvas_id, int user_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = canvases_.find(canvas_id);
    if (it != canvases_.end() && it->second) {
        it->second->disconnectUser(user_id);
        std::cout << "[CanvasPool] Disconnected user #" << user_id << " from Canvas #" << canvas_id << "\n";
        if (it->second->getActiveUsers().empty()) {
            canvases_.erase(it);
            std::cout << "[CanvasPool] Canvas #" << canvas_id << " has no active users, unloaded from pool (load -1)\n";
        }
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
