#include "CanvasPool.hpp"
#include "CanvasPassword.hpp"
#include <iostream>

namespace {
bool documentAuthorizesCanvasAccess(const nlohmann::json& doc, int user_id, long long settings_revision) {
    if (!doc.is_object() || !doc.contains("people") || !doc["people"].is_array()) return false;
    long long current_revision = 0;
    if (doc.contains("settings-revision")) {
        if (!doc["settings-revision"].is_number_integer()) return false;
        try {
            current_revision = doc["settings-revision"].get<long long>();
        } catch (...) {
            return false;
        }
    }
    if (current_revision != settings_revision) return false;
    for (const auto& member : doc["people"]) {
        if (!member.is_number_integer()) continue;
        try {
            if (member.get<long long>() == user_id) return true;
        } catch (...) {
            // Ignore malformed participant IDs and fail closed for them.
        }
    }
    return false;
}
}

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
    std::shared_ptr<std::mutex> lifecycle_mtx;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto mtx_it = lifecycle_mutexes_.find(canvas_id);
        if (mtx_it == lifecycle_mutexes_.end()) {
            lifecycle_mtx = std::make_shared<std::mutex>();
            lifecycle_mutexes_[canvas_id] = lifecycle_mtx;
        } else {
            lifecycle_mtx = mtx_it->second;
        }
    }

    // Serialize even the existing-canvas fast path with unload.
    std::lock_guard<std::mutex> lifecycle_lock(*lifecycle_mtx);

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
    const auto redis_info = mssql.getOrAllocateRedisAndSetCached(canvas_id, cpp_server_ip_, cpp_server_port_);
    std::string redis_ip = redis_info.redis_ip;
    int redis_port = redis_info.redis_port;
    const bool redis_was_cached = redis_info.was_cached;

    if (redis_ip == "NOT_FOUND") {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: Canvas does not exist in DB\n";
        return nullptr;
    }

    if (redis_ip == "WRONG_SERVER") {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: Canvas is already allocated to another C++ server\n";
        return nullptr;
    }

    if (redis_ip == "ERROR" || redis_port <= 0) {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: storage allocation failed\n";
        return nullptr;
    }

    // Prefer an existing Redis document during failover. Elasticsearch is the
    // source only when this canvas has no active cache.
    RedisClient redis(redis_ip, redis_port);
    if (!redis.ping()) {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: Redis is unavailable\n";
        if (!redis_was_cached) mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_);
        return nullptr;
    }

    std::optional<nlohmann::json> canvas_doc;
    auto cached_str = redis.get("canvas:" + std::to_string(canvas_id));
    if (cached_str && !cached_str->empty()) {
        try {
            canvas_doc = nlohmann::json::parse(*cached_str);
        } catch (const std::exception& e) {
            std::cerr << "[CanvasPool] Invalid Redis document for canvas #" << canvas_id << ": " << e.what() << "\n";
            canvas_doc.reset();
        }
    }

    if (!canvas_doc.has_value()) {
        if (redis_was_cached) {
            std::cerr << "[CanvasPool] Keeping Canvas #" << canvas_id
                      << " assigned because its active Redis document is missing or invalid\n";
            return nullptr;
        }
        EsClient es(es_host_, es_port_);
        canvas_doc = es.getCanvasDocument(canvas_id);
        if (!canvas_doc.has_value()) {
            std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << " initialization: no durable document is available\n";
            mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_);
            return nullptr;
        }
    }

    // Canonicalize legacy password fields before an active document can be
    // observed or eventually flushed back to Elasticsearch.
    bool password_migrated = false;
    for (const char* legacy : {"canvas-password", "canvasPassword", "canvas_password_hash"}) {
        if (!canvas_doc->contains(legacy)) continue;
        if (!canvas_doc->contains("canvas-password-hash")) (*canvas_doc)["canvas-password-hash"] = (*canvas_doc)[legacy];
        canvas_doc->erase(legacy);
        password_migrated = true;
    }
    if (canvas_doc->contains("canvas-password-hash") && (*canvas_doc)["canvas-password-hash"].is_string()) {
        auto normalized = normalizeCanvasPassword((*canvas_doc)["canvas-password-hash"].get<std::string>());
        if (!normalized) {
            std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << ": password hashing failed\n";
            if (!redis_was_cached) mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_);
            return nullptr;
        }
        if ((*canvas_doc)["canvas-password-hash"].get<std::string>() != *normalized) password_migrated = true;
        (*canvas_doc)["canvas-password-hash"] = *normalized;
    }
    if (password_migrated && !EsClient(es_host_, es_port_).saveCanvasDocument(canvas_id, *canvas_doc)) {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << ": legacy password migration failed\n";
        if (!redis_was_cached) mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_);
        return nullptr;
    }
    if (!redis.set("canvas:" + std::to_string(canvas_id), canvas_doc->dump())) {
        std::cerr << "[CanvasPool] Rejecting canvas #" << canvas_id << ": Redis document write failed\n";
        if (!redis_was_cached) {
            const bool cleaned = redis.deletePattern("canvas:" + std::to_string(canvas_id) + ":*")
                && redis.del("canvas:" + std::to_string(canvas_id));
            if (cleaned) {
                mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_);
            } else {
                std::cerr << "[CanvasPool] Keeping Canvas #" << canvas_id
                          << " assigned because its Redis state could not be cleared after initialization failed\n";
            }
        }
        return nullptr;
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

bool CanvasPool::isCanvasAccessAuthorized(int canvas_id, int user_id, long long settings_revision) {
    if (canvas_id <= 0 || user_id <= 0) return false;

    MssqlClient mssql(db_host_, db_port_);
    const auto assignment = mssql.getCanvasStorageAssignment(canvas_id);
    if (!assignment) return false;

    if (assignment->is_cached && !assignment->cpp_server_ip.empty()
        && (assignment->cpp_server_ip != cpp_server_ip_
            || assignment->cpp_server_port != std::to_string(cpp_server_port_))) {
        std::cerr << "[CanvasPool] Access preflight rejected Canvas #" << canvas_id
                  << " because it is assigned to another C++ server\n";
        return false;
    }

    if (auto canvas = getCanvas(canvas_id)) {
        std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
        if (canvas->unloading) return false;
        RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
        const auto raw = redis.get("canvas:" + std::to_string(canvas_id));
        if (!raw || raw->empty()) return false;
        try {
            return documentAuthorizesCanvasAccess(nlohmann::json::parse(*raw), user_id, settings_revision);
        } catch (const std::exception& e) {
            std::cerr << "[CanvasPool] Participant preflight could not parse active Canvas #"
                      << canvas_id << ": " << e.what() << "\n";
            return false;
        }
    }

    if (assignment->is_cached) {
        if (assignment->redis_ip.empty() || assignment->redis_port <= 0) return false;
        RedisClient redis(assignment->redis_ip, assignment->redis_port);
        const auto raw = redis.get("canvas:" + std::to_string(canvas_id));
        if (!raw || raw->empty()) return false;
        try {
            return documentAuthorizesCanvasAccess(nlohmann::json::parse(*raw), user_id, settings_revision);
        } catch (const std::exception& e) {
            std::cerr << "[CanvasPool] Access preflight could not parse Redis Canvas #"
                      << canvas_id << ": " << e.what() << "\n";
            return false;
        }
    }

    // Uncached canvases use the durable document as their authorization source.
    EsClient es(es_host_, es_port_);
    const auto doc = es.getCanvasDocument(canvas_id);
    return doc && documentAuthorizesCanvasAccess(*doc, user_id, settings_revision);
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

bool CanvasPool::unloadCanvas(int canvas_id, std::shared_ptr<Canvas> canvas) {
    if (!canvas) return false;
    std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);
    canvas->waitForPendingPersistence(settings_lock);

    // 1. Disconnect all connected users
    canvas->disconnectAll();

    // 2. Fetch canvas document from Redis and reflect to Elasticsearch
    nlohmann::json final_doc;
    try {
        RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
        auto cached_str = redis.get("canvas:" + std::to_string(canvas_id));
        if (!cached_str || cached_str->empty()) {
            std::cerr << "[CanvasPool] Keeping Canvas #" << canvas_id
                      << " assigned because its active Redis document could not be read\n";
            return false;
        }
        final_doc = nlohmann::json::parse(*cached_str);
        EsClient es(es_host_, es_port_);
        if (!es.saveCanvasDocument(canvas_id, final_doc)) {
            std::cerr << "[CanvasPool] Keeping Redis cache for canvas #" << canvas_id
                      << " because Elasticsearch persistence failed\n";
            return false;
        }
        std::cout << "[CanvasPool] Reflected Canvas #" << canvas_id << " from Redis to Elasticsearch\n";

        // 3. Clean up Redis cache
        if (!redis.deletePattern("canvas:" + std::to_string(canvas_id) + ":*")
            || !redis.del("canvas:" + std::to_string(canvas_id))) {
            std::cerr << "[CanvasPool] Keeping Canvas #" << canvas_id
                      << " assigned because its Redis cache could not be fully removed\n";
            redis.set("canvas:" + std::to_string(canvas_id), final_doc.dump());
            return false;
        }
        std::cout << "[CanvasPool] Cleaned up Redis cache for Canvas #" << canvas_id << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[CanvasPool] Error during Redis/ES sync for canvas #" << canvas_id << ": " << e.what() << "\n";
        return false;
    }

    // 4. Update MS SQL canvas_info: is_cached=false, redis/server ip&port=none(NULL)
    try {
        MssqlClient mssql(db_host_, db_port_);
        if (!mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_)) {
            const auto assignment = mssql.getCanvasStorageAssignment(canvas_id);
            if (assignment && (!assignment->is_cached
                || assignment->cpp_server_ip != cpp_server_ip_
                || assignment->cpp_server_port != std::to_string(cpp_server_port_))) return true;

            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            if (!redis.set("canvas:" + std::to_string(canvas_id), final_doc.dump())) {
                std::cerr << "[CanvasPool] Failed to restore Canvas #" << canvas_id
                          << " to Redis after its MSSQL assignment could not be cleared\n";
            }
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[CanvasPool] Error updating MS SQL for canvas #" << canvas_id << ": " << e.what() << "\n";
        RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
        redis.set("canvas:" + std::to_string(canvas_id), final_doc.dump());
        return false;
    }

    std::cout << "[CanvasPool] Canvas #" << canvas_id << " has no active users, unloaded from pool (load -1)\n";
    return true;
}

bool CanvasPool::removeCanvas(int canvas_id) {
    std::shared_ptr<std::mutex> lifecycle_mtx;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto& entry = lifecycle_mutexes_[canvas_id];
        if (!entry) entry = std::make_shared<std::mutex>();
        lifecycle_mtx = entry;
    }
    std::lock_guard<std::mutex> lifecycle_lock(*lifecycle_mtx);

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
        if (!unloadCanvas(canvas_id, canvas)) {
            {
                std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
                canvas->unloading = false;
            }
            std::lock_guard<std::mutex> lock(pool_mutex_);
            canvases_[canvas_id] = canvas;
        }
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

std::optional<std::uint64_t> CanvasPool::updateUserSessionConnected(int user_id, int canvas_id) {
    std::shared_ptr<std::mutex> session_mutex;
    {
        std::lock_guard<std::mutex> lock(session_generation_mutex_);
        auto& entry = user_session_mutexes_[user_id];
        if (!entry) entry = std::make_shared<std::mutex>();
        session_mutex = entry;
    }
    std::lock_guard<std::mutex> session_lock(*session_mutex);
    MssqlClient mssql(db_host_, db_port_);
    if (!mssql.updateUserSessionConnected(user_id, canvas_id, cpp_server_ip_, cpp_server_port_)) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> generation_lock(session_generation_mutex_);
    return ++user_session_generations_[user_id];
}

bool CanvasPool::updateUserSessionDisconnected(int user_id, int canvas_id, std::uint64_t session_generation) {
    std::shared_ptr<std::mutex> session_mutex;
    {
        std::lock_guard<std::mutex> lock(session_generation_mutex_);
        auto& entry = user_session_mutexes_[user_id];
        if (!entry) entry = std::make_shared<std::mutex>();
        session_mutex = entry;
    }
    std::lock_guard<std::mutex> session_lock(*session_mutex);
    {
        std::lock_guard<std::mutex> generation_lock(session_generation_mutex_);
        auto it = user_session_generations_.find(user_id);
        if (it == user_session_generations_.end() || it->second != session_generation) return false;
    }
    MssqlClient mssql(db_host_, db_port_);
    return mssql.updateUserSessionDisconnected(user_id, canvas_id, cpp_server_ip_, cpp_server_port_);
}
