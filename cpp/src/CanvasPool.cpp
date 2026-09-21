#include "CanvasPool.hpp"
#include <iostream>

CanvasPool::CanvasPool(const std::string& db_host, int db_port,
                       const std::string& es_host, int es_port,
                       const std::string& java_host, int java_port,
                       const std::string& cpp_server_ip, int cpp_server_port)
    : db_host_(db_host), db_port_(db_port), es_host_(es_host), es_port_(es_port),
      java_host_(java_host), java_port_(java_port),
      cpp_server_ip_(cpp_server_ip), cpp_server_port_(cpp_server_port) {
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
    std::shared_ptr<std::mutex> init_mtx;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(canvas_id);
        if (it != canvases_.end()) {
            return it->second;
        }

        auto mtx_it = loading_mutexes_.find(canvas_id);
        if (mtx_it == loading_mutexes_.end()) {
            init_mtx = std::make_shared<std::mutex>();
            loading_mutexes_[canvas_id] = init_mtx;
        } else {
            init_mtx = mtx_it->second;
        }
    }

    // Lock the initialization mutex for this specific canvas
    std::lock_guard<std::mutex> init_lock(*init_mtx);

    // Double check if another thread initialized it while we were waiting
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(canvas_id);
        if (it != canvases_.end()) {
            return it->second;
        }
    }

    std::cout << "[CanvasPool] Canvas #" << canvas_id << " not in pool. Initializing from MSSQL & ES...\n";

    // 1) Allocate Redis and set cached=1 in DB exactly once when Canvas is created
    MssqlClient mssql(db_host_, db_port_);
    auto redis_info = mssql.getOrAllocateRedisAndSetCached(canvas_id, cpp_server_ip_, cpp_server_port_);
    std::string redis_ip = redis_info.first;
    int redis_port = redis_info.second;

    if (redis_ip == "NOT_FOUND") {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: Canvas does not exist in DB\n";
        std::lock_guard<std::mutex> lock(pool_mutex_);
        loading_mutexes_.erase(canvas_id);
        return nullptr;
    }

    if (redis_ip == "WRONG_SERVER") {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: Canvas is already allocated to another C++ server\n";
        std::lock_guard<std::mutex> lock(pool_mutex_);
        loading_mutexes_.erase(canvas_id);
        return nullptr;
    }

    if (redis_ip == "ERROR" || redis_port <= 0) {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: storage allocation failed\n";
        std::lock_guard<std::mutex> lock(pool_mutex_);
        loading_mutexes_.erase(canvas_id);
        return nullptr;
    }

    // Prefer an existing Redis document during failover. Elasticsearch is the
    // source only when this canvas has no active cache.
    RedisClient redis(redis_ip, redis_port);
    if (!redis.ping()) {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: Redis is unavailable\n";
        mssql.updateCanvasUncached(canvas_id);
        std::lock_guard<std::mutex> lock(pool_mutex_);
        loading_mutexes_.erase(canvas_id);
        return nullptr;
    }

    std::optional<nlohmann::json> canvas_doc;
    auto cached_str = redis.get("canvas:" + std::to_string(canvas_id));
    if (cached_str && !cached_str->empty()) {
        try {
            canvas_doc = nlohmann::json::parse(*cached_str);
            // Rewriting migrates legacy string values to RedisJSON.
            if (!redis.set("canvas:" + std::to_string(canvas_id), canvas_doc->dump())) {
                throw std::runtime_error("failed to migrate cached document to RedisJSON");
            }
        } catch (const std::exception& e) {
            std::cerr << "[CanvasPool] Invalid Redis document for canvas #" << canvas_id << ": " << e.what() << "\n";
            canvas_doc.reset();
        }
    }

    if (!canvas_doc.has_value()) {
        EsClient es(es_host_, es_port_);
        canvas_doc = es.getCanvasDocument(canvas_id);
        if (!canvas_doc.has_value() || !redis.set("canvas:" + std::to_string(canvas_id), canvas_doc->dump())) {
            std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: no durable document is available\n";
            mssql.updateCanvasUncached(canvas_id);
            std::lock_guard<std::mutex> lock(pool_mutex_);
            loading_mutexes_.erase(canvas_id);
            return nullptr;
        }
    }

    std::string canvas_name = "Canvas-" + std::to_string(canvas_id);
    int admin_uid = 0;

    if (canvas_doc->contains("canvas-name") && (*canvas_doc)["canvas-name"].is_string()) {
        canvas_name = (*canvas_doc)["canvas-name"].get<std::string>();
    }
    if (canvas_doc->contains("admin-user-id") && (*canvas_doc)["admin-user-id"].is_number_integer()) {
        admin_uid = (*canvas_doc)["admin-user-id"].get<int>();
    }

    // 4. Create and register Canvas in pool
    auto canvas = std::make_shared<Canvas>(canvas_id, redis_ip, redis_port);
    canvas->setCanvasName(canvas_name);
    canvas->setAdminUserId(admin_uid);
    canvas->setWebSocketCallbacks(web_socket_callbacks_);

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        canvases_[canvas_id] = canvas;
        loading_mutexes_.erase(canvas_id);
    }
    
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
            if (!es.saveCanvasDocument(canvas_id, doc)) {
                std::cerr << "[CanvasPool] Keeping Redis cache for canvas #" << canvas_id
                          << " because Elasticsearch persistence failed\n";
                return;
            }
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
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto it = canvases_.begin(); it != canvases_.end(); ++it) {
        if (it->second && it->second->isUserActive(user_id)) {
            it->second->disconnectUserCompletely(user_id);
            std::cout << "[CanvasPool] Disconnected user #" << user_id << " from Canvas #" << it->first << "\n";
        }
    }
}

void CanvasPool::disconnectUser(int canvas_id, int user_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = canvases_.find(canvas_id);
    if (it != canvases_.end() && it->second) {
        it->second->disconnectUserCompletely(user_id);
        std::cout << "[CanvasPool] Disconnected user #" << user_id << " from Canvas #" << canvas_id << "\n";
    }
}

int CanvasPool::getActiveCanvasCount() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    int count = 0;
    for (const auto& [id, canvas] : canvases_) {
        (void)id;
        if (canvas && !canvas->getActiveUsers().empty()) ++count;
    }
    return count;
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

void CanvasPool::cleanupInactiveCanvases() {
    std::vector<int> active_ids = getActiveCanvasIds();
    MssqlClient mssql(db_host_, db_port_);
    
    for (int canvas_id : active_ids) {
        if (!mssql.isCanvasActiveInDb(canvas_id)) {
            std::cout << "[CanvasPool] Cleanup task found no active users for Canvas #" << canvas_id << ". Unloading.\n";
            removeCanvas(canvas_id);
        }
    }
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

bool CanvasPool::updateUserSessionConnected(int user_id, int canvas_id) {
    MssqlClient mssql(db_host_, db_port_);
    return mssql.updateUserSessionConnected(user_id, canvas_id, cpp_server_ip_, cpp_server_port_);
}

bool CanvasPool::updateUserSessionDisconnected(int user_id) {
    MssqlClient mssql(db_host_, db_port_);
    return mssql.updateUserSessionDisconnected(user_id);
}
