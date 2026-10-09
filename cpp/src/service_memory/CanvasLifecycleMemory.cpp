#include "service_memory/CanvasLifecycleMemory.hpp"
#include "service_memory/CanvasServiceMemory.hpp"
#include "CanvasPassword.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>

namespace {
std::string newCacheGeneration() {
    static std::atomic<std::uint64_t> sequence{0};
    std::uint64_t random_value = 0;
    try {
        std::random_device random;
        random_value = (static_cast<std::uint64_t>(random()) << 32) | random();
    } catch (...) {
        random_value = static_cast<std::uint64_t>(
            std::chrono::system_clock::now().time_since_epoch().count());
    }
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
        + "-" + std::to_string(random_value)
        + "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

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

CanvasLifecycleMemory::CanvasLifecycleMemory(const std::string& db_host, int db_port,
                       const std::string& es_host, int es_port,
                       const std::string& java_host, int java_port,
                       const std::string& cpp_server_ip, int cpp_server_port)
    : db_host_(db_host), db_port_(db_port), es_host_(es_host), es_port_(es_port),
      java_host_(java_host), java_port_(java_port),
      cpp_server_ip_(cpp_server_ip), cpp_server_port_(cpp_server_port) {
}

CanvasLifecycleMemory::~CanvasLifecycleMemory() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    canvases_.clear();
}

std::shared_ptr<std::mutex> CanvasLifecycleMemory::lifecycleMutexForCanvas(int canvas_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto& lifecycle_mtx = lifecycle_mutexes_[canvas_id];
    if (!lifecycle_mtx) lifecycle_mtx = std::make_shared<std::mutex>();
    return lifecycle_mtx;
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::getOrCreateCanvasWithLifecycleLock(int canvas_id) {
    // Double check if another thread initialized it while we were waiting
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(canvas_id);
        if (it != canvases_.end()) {
            return it->second;
        }
    }

    std::cout << "[CanvasLifecycleMemory] Canvas #" << canvas_id << " not in pool. Initializing from MSSQL & ES...\n";

    // 1) Allocate Redis and set cached=1 in DB exactly once when Canvas is created
    RegistryServiceMemory mssql(db_host_, db_port_);
    const auto redis_info = mssql.getOrAllocateRedisAndSetCached(canvas_id, cpp_server_ip_, cpp_server_port_);
    std::string redis_ip = redis_info.redis_ip;
    int redis_port = redis_info.redis_port;
    const bool redis_was_cached = redis_info.was_cached;

    if (redis_ip == "NOT_FOUND") {
        std::cerr << "[CanvasLifecycleMemory] Rejecting canvas #" << canvas_id << " initialization: Canvas does not exist in DB\n";
        return nullptr;
    }

    if (redis_ip == "WRONG_SERVER") {
        std::cerr << "[CanvasLifecycleMemory] Rejecting canvas #" << canvas_id << " initialization: Canvas is already allocated to another C++ server\n";
        return nullptr;
    }

    if (redis_ip == "ERROR" || redis_port <= 0) {
        std::cerr << "[CanvasLifecycleMemory] Rejecting canvas #" << canvas_id << " initialization: storage allocation failed\n";
        return nullptr;
    }

    // Prefer an existing Redis document during failover. Elasticsearch is the
    // source only when this canvas has no active cache.
    CanvasServiceMemory memory(redis_ip, redis_port);
    if (!memory.ping()) {
        std::cerr << "[CanvasLifecycleMemory] Rejecting canvas #" << canvas_id << " initialization: Redis is unavailable\n";
        if (!redis_was_cached) mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_);
        return nullptr;
    }

    CanvasSnapshotMemory snapshots(es_host_, es_port_);
    auto canvas_doc = memory.initializeCanvas(canvas_id, snapshots, redis_was_cached, newCacheGeneration());
    if (!canvas_doc) {
        std::cerr << "[CanvasLifecycleMemory] Rejecting canvas #" << canvas_id << ": memory initialization failed\n";
        if (!redis_was_cached && memory.clearCanvas(canvas_id))
            mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_);
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
    canvas->setSettingsRevision(canvas_doc->value("settings-revision", 0LL));
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        canvases_[canvas_id] = canvas;
    }

    std::cout << "[CanvasLifecycleMemory] Canvas #" << canvas_id << " successfully created and registered in pool\n";
    return canvas;
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::getCanvas(int canvas_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = canvases_.find(canvas_id);
    if (it != canvases_.end()) {
        return it->second;
    }
    return nullptr;
}

bool CanvasLifecycleMemory::isCanvasAccessAuthorized(int canvas_id, int user_id, long long settings_revision) {
    return getAuthorizedCanvasDocument(canvas_id, user_id, settings_revision).has_value();
}

std::optional<nlohmann::json> CanvasLifecycleMemory::getAuthorizedCanvasDocument(
        int canvas_id, int user_id, long long settings_revision) {
    if (canvas_id <= 0 || user_id <= 0) return std::nullopt;

    // Serialize the authorization source selection with unload. Otherwise a
    // request can read the old cached assignment, wait for unload to finish,
    // and then reject against a Canvas object that has already left the pool.
    auto lifecycle_mtx = lifecycleMutexForCanvas(canvas_id);
    std::lock_guard<std::mutex> lifecycle_lock(*lifecycle_mtx);

    RegistryServiceMemory mssql(db_host_, db_port_);
    const auto assignment = mssql.getCanvasStorageAssignment(canvas_id);
    if (!assignment) return std::nullopt;

    if (assignment->is_cached && !assignment->cpp_server_ip.empty()
        && (assignment->cpp_server_ip != cpp_server_ip_
            || assignment->cpp_server_port != std::to_string(cpp_server_port_))) {
        std::cerr << "[CanvasLifecycleMemory] Access preflight rejected Canvas #" << canvas_id
                  << " because it is assigned to another C++ server\n";
        return std::nullopt;
    }

    if (auto canvas = getCanvas(canvas_id)) {
        std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
        if (canvas->unloading) return std::nullopt;
        CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
        const auto raw = memory.loadCanvas(canvas_id);
        if (!raw || raw->empty()) return std::nullopt;
        try {
            auto doc = nlohmann::json::parse(*raw);
            if (documentAuthorizesCanvasAccess(doc, user_id, settings_revision)) return doc;
            return std::nullopt;
        } catch (const std::exception& e) {
            std::cerr << "[CanvasLifecycleMemory] Participant preflight could not parse active Canvas #"
                      << canvas_id << ": " << e.what() << "\n";
            return std::nullopt;
        }
    }

    if (assignment->is_cached) {
        if (assignment->redis_ip.empty() || assignment->redis_port <= 0) return std::nullopt;
        CanvasServiceMemory memory(assignment->redis_ip, assignment->redis_port);
        const auto raw = memory.loadCanvas(canvas_id);
        if (!raw || raw->empty()) return std::nullopt;
        try {
            auto doc = nlohmann::json::parse(*raw);
            if (documentAuthorizesCanvasAccess(doc, user_id, settings_revision)) return doc;
            return std::nullopt;
        } catch (const std::exception& e) {
            std::cerr << "[CanvasLifecycleMemory] Access preflight could not parse Redis Canvas #"
                      << canvas_id << ": " << e.what() << "\n";
            return std::nullopt;
        }
    }

    // Uncached canvases use the durable document as their authorization source.
    CanvasSnapshotMemory es(es_host_, es_port_);
    const auto doc = es.getCanvasDocument(canvas_id);
    if (doc && documentAuthorizesCanvasAccess(*doc, user_id, settings_revision)) return doc;
    return std::nullopt;
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::loadForQuery(int canvas_id) {
    auto lifecycle = lifecycleMutexForCanvas(canvas_id);
    std::lock_guard<std::mutex> guard(*lifecycle);
    auto canvas = getOrCreateCanvasWithLifecycleLock(canvas_id);
    if (canvas && !canvas->unloading) canvas->touchQuery();
    return canvas;
}

bool CanvasLifecycleMemory::saveCanvasSnapshotToElasticsearch(
        int canvas_id, const std::shared_ptr<Canvas>& canvas,
        std::unique_lock<std::mutex>& settings_lock, std::string& cache_generation) {
    if (!canvas || !settings_lock.owns_lock()) return false;
    std::cout << "[CanvasLifecycleMemory] Waiting for Canvas #" << canvas_id
              << " pending Redis writes before Elasticsearch snapshot\n";
    if (!canvas->waitForPendingPersistence(settings_lock)) {
        std::cerr << "[CanvasLifecycleMemory] Keeping Canvas #" << canvas_id
                  << " assigned because a Redis write failed; Elasticsearch snapshot is unsafe\n";
        return false;
    }
    std::cout << "[CanvasLifecycleMemory] Canvas #" << canvas_id
              << " Redis writes drained; starting Elasticsearch snapshot\n";

    nlohmann::json final_doc;
    try {
        CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
        if (!memory.flushCanvas(canvas_id)) {
            std::cerr << "[CanvasLifecycleMemory] Keeping Canvas #" << canvas_id
                      << " assigned because its LRU items could not be synchronized to Redis\n";
            return false;
        }
        auto cached_str = memory.loadCanvas(canvas_id);
        if (!cached_str || cached_str->empty()) {
            std::cerr << "[CanvasLifecycleMemory] Keeping Canvas #" << canvas_id
                      << " assigned because its active Redis document could not be read\n";
            return false;
        }
        final_doc = nlohmann::json::parse(*cached_str);
        if (!final_doc.is_object() || !final_doc.contains("_cache_generation")
            || !final_doc["_cache_generation"].is_string()) {
            std::cerr << "[CanvasLifecycleMemory] Canvas #" << canvas_id
                      << " has no cache generation; keeping its Redis assignment\n";
            return false;
        }
        cache_generation = final_doc["_cache_generation"].get<std::string>();
        final_doc.erase("_cache_generation");
        CanvasSnapshotMemory es(es_host_, es_port_);
        if (!es.saveCanvasDocument(canvas_id, final_doc)) {
            std::cerr << "[CanvasLifecycleMemory] Keeping Redis cache for canvas #" << canvas_id
                      << " because Elasticsearch persistence failed\n";
            return false;
        }
        std::cout << "[CanvasLifecycleMemory] Reflected Canvas #" << canvas_id << " from Redis to Elasticsearch\n";
    } catch (const std::exception& e) {
        std::cerr << "[CanvasLifecycleMemory] Error during Redis/ES sync for canvas #" << canvas_id << ": " << e.what() << "\n";
        return false;
    }
    return true;
}

bool CanvasLifecycleMemory::saveCanvasesForShutdown() {
    std::vector<std::pair<int, std::shared_ptr<Canvas>>> canvases;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        canvases.reserve(canvases_.size());
        for (const auto& [canvas_id, canvas] : canvases_) {
            if (canvas) canvases.emplace_back(canvas_id, canvas);
        }
    }
    std::sort(canvases.begin(), canvases.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });

    // Keep each canvas lifecycle stable while shutdown freezes writes and
    // persists snapshots. Sorting gives concurrent cleanup the same lock order.
    std::vector<std::unique_lock<std::mutex>> lifecycle_locks;
    lifecycle_locks.reserve(canvases.size());
    for (const auto& [canvas_id, canvas] : canvases) {
        (void)canvas;
        auto lifecycle_mutex = lifecycleMutexForCanvas(canvas_id);
        lifecycle_locks.emplace_back(*lifecycle_mutex);
    }

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        canvases.erase(std::remove_if(canvases.begin(), canvases.end(), [this](const auto& entry) {
            const auto current = canvases_.find(entry.first);
            return current == canvases_.end() || current->second != entry.second;
        }), canvases.end());
    }

    // Freeze every canvas before snapshotting any of them so no canvas keeps
    // accepting writes while another canvas is being saved.
    for (const auto& [canvas_id, canvas] : canvases) {
        (void)canvas_id;
        std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
        canvas->unloading.store(true);
    }

    bool all_saved = true;
    for (const auto& [canvas_id, canvas] : canvases) {
        std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);
        std::string cache_generation;
        if (!saveCanvasSnapshotToElasticsearch(canvas_id, canvas, settings_lock, cache_generation)) {
            all_saved = false;
        }
    }
    return all_saved;
}

bool CanvasLifecycleMemory::unloadCanvas(int canvas_id, std::shared_ptr<Canvas> canvas) {
    if (!canvas) return false;
    std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);

    // Active users and SQL reservations were checked under the lifecycle lock.
    // Do not queue a disconnect-all callback here: it could run after a new
    // Canvas instance is loaded for this ID and close its fresh sockets.

    // Keep the Redis root until MSSQL says uncached: Spring holds the SQL
    // canvas row lock while choosing Redis or Elasticsearch.
    std::string cache_generation;
    if (!saveCanvasSnapshotToElasticsearch(canvas_id, canvas, settings_lock, cache_generation)) {
        return false;
    }

    // 2. Update MS SQL first. A new owner may load this canvas immediately
    // afterwards, so the eventual Redis deletion must compare generations.
    try {
        RegistryServiceMemory mssql(db_host_, db_port_);
        if (!mssql.updateCanvasUncached(canvas_id, cpp_server_ip_, cpp_server_port_)) {
            const auto assignment = mssql.getCanvasStorageAssignment(canvas_id);
            if (assignment && (!assignment->is_cached
                || assignment->cpp_server_ip != cpp_server_ip_
                || assignment->cpp_server_port != std::to_string(cpp_server_port_))) return true;

            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[CanvasLifecycleMemory] Error updating MS SQL for canvas #" << canvas_id << ": " << e.what() << "\n";
        return false;
    }

    // 3. Remove only the snapshot owned by this unload. If another load has
    // already replaced it, leave the new cache untouched.
    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    const auto cleanup = memory.removeCanvasGeneration(canvas_id, cache_generation);
    if (cleanup == CanvasServiceMemory::CompareSetResult::Error) {
        std::cerr << "[CanvasLifecycleMemory] Canvas #" << canvas_id
                  << " is uncached in MS SQL, but old Redis snapshot cleanup failed\n";
    } else if (cleanup == CanvasServiceMemory::CompareSetResult::Applied) {
        std::cout << "[CanvasLifecycleMemory] Cleaned up Redis cache for Canvas #" << canvas_id << "\n";
    }

    std::cout << "[CanvasLifecycleMemory] Canvas #" << canvas_id << " has no active users, unloaded from pool (load -1)\n";
    return true;
}

bool CanvasLifecycleMemory::removeCanvas(int canvas_id) {
    return removeCanvasImpl(canvas_id);
}

bool CanvasLifecycleMemory::removeCanvasImpl(int canvas_id) {
    auto lifecycle_mtx = lifecycleMutexForCanvas(canvas_id);
    std::lock_guard<std::mutex> lifecycle_lock(*lifecycle_mtx);

    std::shared_ptr<Canvas> canvas;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(canvas_id);
        if (it != canvases_.end()) {
            canvas = it->second;
        }
    }

    if (!canvas) return false;

    if (canvas->hasRecentQuery()) return false;

    // Keep the shared instance discoverable while unload marks it as closing
    // and drains persistence. Removing it first lets concurrent close/auth
    // callbacks observe a missing canvas while its Redis and SQL state is
    // still active, and can also race a fresh initialization for this ID.
    if (!unloadCanvas(canvas_id, canvas)) {
        std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
        canvas->unloading = false;
        return false;
    }

    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = canvases_.find(canvas_id);
    if (it != canvases_.end() && it->second == canvas) canvases_.erase(it);
    return true;
}

int CanvasLifecycleMemory::getActiveCanvasCount() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    int count = 0;
    for (const auto& [id, canvas] : canvases_) {
        (void)id;
        if (canvas && canvas->hasRecentQuery()) ++count;
    }
    return count;
}

std::vector<int> CanvasLifecycleMemory::getActiveCanvasIds() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    std::vector<int> ids;
    ids.reserve(canvases_.size());
    for (const auto& [id, canvas] : canvases_) {
        ids.push_back(id);
    }
    return ids;
}

void CanvasLifecycleMemory::cleanupInactiveCanvases() {
    std::vector<int> active_ids = getActiveCanvasIds();
    for (int canvas_id : active_ids) {
        if (removeCanvasImpl(canvas_id)) {
            std::cout << "[CanvasLifecycleMemory] Cleanup task found no active users for Canvas #" << canvas_id << ". Unloading.\n";
        }
    }
}

