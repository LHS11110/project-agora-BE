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
    if (doc.contains("visibility") && doc["visibility"] == "public") return true;
    for (const auto& member : doc["people"]) {
        if (member.is_number_integer() && member.get<int>() == user_id) return true;
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

std::shared_ptr<std::mutex> CanvasLifecycleMemory::lifecycleMutexForSpace(SpaceKey key) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto& lifecycle_mtx = lifecycle_mutexes_[key];
    if (!lifecycle_mtx) lifecycle_mtx = std::make_shared<std::mutex>();
    return lifecycle_mtx;
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::getOrCreateCanvasWithLifecycleLock(SpaceKey key) {
    // Double check if another thread initialized it while we were waiting
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(key);
        if (it != canvases_.end()) {
            return it->second;
        }
    }

    const std::string scope_wire = std::string(spaceScopeWire(key.scope));
    std::cout << "[CanvasLifecycleMemory] " << scope_wire << " #" << key.id << " not in pool. Initializing from MSSQL & ES...\n";

    // 1) Allocate Redis and set cached=1 in DB exactly once when space is initialized
    RegistryServiceMemory mssql(db_host_, db_port_);
    const auto redis_info = mssql.getOrAllocateRedisAndSetCached(key.scope, key.id, cpp_server_ip_, cpp_server_port_);
    std::string redis_ip = redis_info.redis_ip;
    int redis_port = redis_info.redis_port;
    const bool redis_was_cached = redis_info.was_cached;

    if (redis_ip == "NOT_FOUND") {
        std::cerr << "[CanvasLifecycleMemory] Rejecting " << scope_wire << " #" << key.id << " initialization: does not exist in DB\n";
        return nullptr;
    }

    if (redis_ip == "WRONG_SERVER") {
        std::cerr << "[CanvasLifecycleMemory] Rejecting " << scope_wire << " #" << key.id << " initialization: already allocated to another C++ server\n";
        return nullptr;
    }

    if (redis_ip == "ERROR" || redis_port <= 0) {
        std::cerr << "[CanvasLifecycleMemory] Rejecting " << scope_wire << " #" << key.id << " initialization: storage allocation failed\n";
        return nullptr;
    }

    // Prefer an existing Redis document during failover. Elasticsearch is the
    // source only when this space has no active cache.
    CanvasServiceMemory memory(redis_ip, redis_port);
    if (!memory.ping()) {
        std::cerr << "[CanvasLifecycleMemory] Rejecting " << scope_wire << " #" << key.id << " initialization: Redis is unavailable\n";
        if (!redis_was_cached) mssql.updateCanvasUncached(key.scope, key.id, cpp_server_ip_, cpp_server_port_);
        return nullptr;
    }

    CanvasSnapshotMemory snapshots(es_host_, es_port_, "", "", "", key.scope);
    auto canvas_doc = memory.initializeCanvas(key.scope, key.id, snapshots, redis_was_cached, newCacheGeneration());
    if (!canvas_doc) {
        std::cerr << "[CanvasLifecycleMemory] Rejecting " << scope_wire << " #" << key.id << ": memory initialization failed\n";
        if (!redis_was_cached && memory.clearCanvas(key.scope, key.id))
            mssql.updateCanvasUncached(key.scope, key.id, cpp_server_ip_, cpp_server_port_);
        return nullptr;
    }

    std::string canvas_name = (key.scope == SpaceScope::Workspace ? "Workspace-" : "Canvas-") + std::to_string(key.id);
    int admin_uid = 0;

    if (canvas_doc->contains("canvas-name") && (*canvas_doc)["canvas-name"].is_string()) {
        canvas_name = (*canvas_doc)["canvas-name"].get<std::string>();
    }
    if (canvas_doc->contains("admin-user-id") && (*canvas_doc)["admin-user-id"].is_number_integer()) {
        admin_uid = (*canvas_doc)["admin-user-id"].get<int>();
    }

    // 4. Create and register Canvas/Workspace in pool
    auto canvas = std::make_shared<Canvas>(key.scope, key.id, redis_ip, redis_port);
    canvas->setCanvasName(canvas_name);
    canvas->setAdminUserId(admin_uid);
    canvas->setSettingsRevision(canvas_doc->value("settings-revision", 0LL));
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        canvases_[key] = canvas;
    }

    std::cout << "[CanvasLifecycleMemory] " << scope_wire << " #" << key.id << " successfully created and registered in pool\n";
    return canvas;
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::getCanvas(int canvas_id) {
    return getCanvas(SpaceScope::Canvas, canvas_id);
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::getCanvas(SpaceScope scope, int space_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = canvases_.find({scope, space_id});
    if (it != canvases_.end()) {
        return it->second;
    }
    return nullptr;
}

bool CanvasLifecycleMemory::isCanvasAccessAuthorized(int canvas_id, int user_id, long long settings_revision) {
    return isCanvasAccessAuthorized(SpaceScope::Canvas, canvas_id, user_id, settings_revision);
}

bool CanvasLifecycleMemory::isCanvasAccessAuthorized(SpaceScope scope, int space_id, int user_id, long long settings_revision) {
    return getAuthorizedCanvasDocument(scope, space_id, user_id, settings_revision).has_value();
}

std::optional<nlohmann::json> CanvasLifecycleMemory::getAuthorizedCanvasDocument(
        int canvas_id, int user_id, long long settings_revision) {
    return getAuthorizedCanvasDocument(SpaceScope::Canvas, canvas_id, user_id, settings_revision);
}

std::optional<nlohmann::json> CanvasLifecycleMemory::getAuthorizedCanvasDocument(
        SpaceScope scope, int space_id, int user_id, long long settings_revision) {
    if (space_id <= 0 || user_id <= 0) return std::nullopt;
    const SpaceKey key{scope, space_id};

    // Serialize the authorization source selection with unload. Otherwise a
    // request can read the old cached assignment, wait for unload to finish,
    // and then reject against a Canvas object that has already left the pool.
    auto lifecycle_mtx = lifecycleMutexForSpace(key);
    std::lock_guard<std::mutex> lifecycle_lock(*lifecycle_mtx);

    RegistryServiceMemory mssql(db_host_, db_port_);
    const auto assignment = mssql.getCanvasStorageAssignment(scope, space_id);
    if (!assignment) return std::nullopt;

    if (assignment->is_cached && !assignment->cpp_server_ip.empty()
        && (assignment->cpp_server_ip != cpp_server_ip_
            || assignment->cpp_server_port != std::to_string(cpp_server_port_))) {
        std::cerr << "[CanvasLifecycleMemory] Access preflight rejected " << spaceScopeWire(scope) << " #" << space_id
                  << " because it is assigned to another C++ server\n";
        return std::nullopt;
    }

    if (auto canvas = getCanvas(scope, space_id)) {
        std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
        if (canvas->unloading) return std::nullopt;
        CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
        const auto raw = memory.loadCanvas(scope, space_id);
        if (!raw || raw->empty()) return std::nullopt;
        try {
            auto doc = nlohmann::json::parse(*raw);
            if (documentAuthorizesCanvasAccess(doc, user_id, settings_revision)) return doc;
            return std::nullopt;
        } catch (const std::exception& e) {
            std::cerr << "[CanvasLifecycleMemory] Participant preflight could not parse active "
                      << spaceScopeWire(scope) << " #" << space_id << ": " << e.what() << "\n";
            return std::nullopt;
        }
    }

    if (assignment->is_cached) {
        if (assignment->redis_ip.empty() || assignment->redis_port <= 0) return std::nullopt;
        CanvasServiceMemory memory(assignment->redis_ip, assignment->redis_port);
        const auto raw = memory.loadCanvas(scope, space_id);
        if (!raw || raw->empty()) return std::nullopt;
        try {
            auto doc = nlohmann::json::parse(*raw);
            if (documentAuthorizesCanvasAccess(doc, user_id, settings_revision)) return doc;
            return std::nullopt;
        } catch (const std::exception& e) {
            std::cerr << "[CanvasLifecycleMemory] Access preflight could not parse Redis "
                      << spaceScopeWire(scope) << " #" << space_id << ": " << e.what() << "\n";
            return std::nullopt;
        }
    }

    // Uncached spaces use the durable document as their authorization source.
    CanvasSnapshotMemory es(es_host_, es_port_, "", "", "", scope);
    const auto doc = es.getCanvasDocument(space_id);
    if (doc && documentAuthorizesCanvasAccess(*doc, user_id, settings_revision)) return doc;
    return std::nullopt;
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::loadForQuery(int canvas_id) {
    return loadForQuery(SpaceScope::Canvas, canvas_id);
}

std::shared_ptr<Canvas> CanvasLifecycleMemory::loadForQuery(SpaceScope scope, int space_id) {
    const SpaceKey key{scope, space_id};
    auto lifecycle = lifecycleMutexForSpace(key);
    std::lock_guard<std::mutex> guard(*lifecycle);
    auto canvas = getOrCreateCanvasWithLifecycleLock(key);
    if (canvas && !canvas->unloading) canvas->touchQuery();
    return canvas;
}

bool CanvasLifecycleMemory::saveCanvasSnapshotToElasticsearch(
        SpaceKey key, const std::shared_ptr<Canvas>& canvas,
        std::unique_lock<std::mutex>& settings_lock, std::string& cache_generation) {
    if (!canvas || !settings_lock.owns_lock()) return false;
    const std::string scope_wire = std::string(spaceScopeWire(key.scope));
    std::cout << "[CanvasLifecycleMemory] Waiting for " << scope_wire << " #" << key.id
              << " pending Redis writes before Elasticsearch snapshot\n";
    if (!canvas->waitForPendingPersistence(settings_lock)) {
        std::cerr << "[CanvasLifecycleMemory] Keeping " << scope_wire << " #" << key.id
                  << " assigned because a Redis write failed; Elasticsearch snapshot is unsafe\n";
        return false;
    }
    std::cout << "[CanvasLifecycleMemory] " << scope_wire << " #" << key.id
              << " Redis writes drained; starting Elasticsearch snapshot\n";

    nlohmann::json final_doc;
    try {
        CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
        if (!memory.flushCanvas(key.scope, key.id)) {
            std::cerr << "[CanvasLifecycleMemory] Keeping " << scope_wire << " #" << key.id
                      << " assigned because its LRU items could not be synchronized to Redis\n";
            return false;
        }
        auto cached_str = memory.loadCanvas(key.scope, key.id);
        if (!cached_str || cached_str->empty()) {
            std::cerr << "[CanvasLifecycleMemory] Keeping " << scope_wire << " #" << key.id
                      << " assigned because its active Redis document could not be read\n";
            return false;
        }
        final_doc = nlohmann::json::parse(*cached_str);
        if (!final_doc.is_object() || !final_doc.contains("_cache_generation")
            || !final_doc["_cache_generation"].is_string()) {
            std::cerr << "[CanvasLifecycleMemory] " << scope_wire << " #" << key.id
                      << " has no cache generation; keeping its Redis assignment\n";
            return false;
        }
        cache_generation = final_doc["_cache_generation"].get<std::string>();
        final_doc.erase("_cache_generation");
        CanvasSnapshotMemory es(es_host_, es_port_, "", "", "", key.scope);
        if (!es.saveCanvasDocument(key.id, final_doc)) {
            std::cerr << "[CanvasLifecycleMemory] Keeping Redis cache for " << scope_wire << " #" << key.id
                      << " because Elasticsearch persistence failed\n";
            return false;
        }
        if(key.scope==SpaceScope::Canvas){
            RegistryServiceMemory registry(db_host_,db_port_);
            const auto clients=final_doc.value("__broker_sync",nlohmann::json::object()).value("clients",nlohmann::json::object());
            if(!registry.storeCanvasActivity(key.id,clients.dump())){std::cerr<<"[CanvasActivity] SQL flush failed; retaining durable Redis/ES counters\n";return false;}
        }
        std::cout << "[CanvasLifecycleMemory] Reflected " << scope_wire << " #" << key.id << " from Redis to Elasticsearch\n";
    } catch (const std::exception& e) {
        std::cerr << "[CanvasLifecycleMemory] Error during Redis/ES sync for " << scope_wire << " #" << key.id << ": " << e.what() << "\n";
        return false;
    }
    return true;
}

bool CanvasLifecycleMemory::saveCanvasesForShutdown() {
    std::vector<std::pair<SpaceKey, std::shared_ptr<Canvas>>> canvases;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        canvases.reserve(canvases_.size());
        for (const auto& [key, canvas] : canvases_) {
            if (canvas) canvases.emplace_back(key, canvas);
        }
    }
    std::sort(canvases.begin(), canvases.end(), [](const auto& left, const auto& right) {
        return std::tie(left.first.scope, left.first.id) < std::tie(right.first.scope, right.first.id);
    });

    // Keep each canvas/workspace lifecycle stable while shutdown freezes writes and
    // persists snapshots. Sorting gives concurrent cleanup the same lock order.
    std::vector<std::unique_lock<std::mutex>> lifecycle_locks;
    lifecycle_locks.reserve(canvases.size());
    for (const auto& [key, canvas] : canvases) {
        (void)canvas;
        auto lifecycle_mutex = lifecycleMutexForSpace(key);
        lifecycle_locks.emplace_back(*lifecycle_mutex);
    }

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        canvases.erase(std::remove_if(canvases.begin(), canvases.end(), [this](const auto& entry) {
            const auto current = canvases_.find(entry.first);
            return current == canvases_.end() || current->second != entry.second;
        }), canvases.end());
    }

    // Freeze every space before snapshotting any of them so none keeps
    // accepting writes while another is being saved.
    for (const auto& [key, canvas] : canvases) {
        (void)key;
        std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
        canvas->unloading.store(true);
    }

    bool all_saved = true;
    for (const auto& [key, canvas] : canvases) {
        std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);
        std::string cache_generation;
        if (!saveCanvasSnapshotToElasticsearch(key, canvas, settings_lock, cache_generation)) {
            all_saved = false;
        }
    }
    return all_saved;
}

bool CanvasLifecycleMemory::unloadCanvas(SpaceKey key, std::shared_ptr<Canvas> canvas) {
    if (!canvas) return false;
    std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);
    const std::string scope_wire = std::string(spaceScopeWire(key.scope));

    // Keep the Redis root until MSSQL says uncached: Spring holds the SQL
    // row lock while choosing Redis or Elasticsearch.
    std::string cache_generation;
    if (!saveCanvasSnapshotToElasticsearch(key, canvas, settings_lock, cache_generation)) {
        return false;
    }

    // 2. Update MS SQL first. A new owner may load this space immediately
    // afterwards, so the eventual Redis deletion must compare generations.
    try {
        RegistryServiceMemory mssql(db_host_, db_port_);
        if (!mssql.updateCanvasUncached(key.scope, key.id, cpp_server_ip_, cpp_server_port_)) {
            const auto assignment = mssql.getCanvasStorageAssignment(key.scope, key.id);
            if (assignment && (!assignment->is_cached
                || assignment->cpp_server_ip != cpp_server_ip_
                || assignment->cpp_server_port != std::to_string(cpp_server_port_))) return true;

            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[CanvasLifecycleMemory] Error updating MS SQL for " << scope_wire << " #" << key.id << ": " << e.what() << "\n";
        return false;
    }

    // 3. Remove only the snapshot owned by this unload. If another load has
    // already replaced it, leave the new cache untouched.
    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    const auto cleanup = memory.removeCanvasGeneration(key.scope, key.id, cache_generation);
    if (cleanup == CanvasServiceMemory::CompareSetResult::Error) {
        std::cerr << "[CanvasLifecycleMemory] " << scope_wire << " #" << key.id
                  << " is uncached in MS SQL, but old Redis snapshot cleanup failed\n";
    } else if (cleanup == CanvasServiceMemory::CompareSetResult::Applied) {
        std::cout << "[CanvasLifecycleMemory] Cleaned up Redis cache for " << scope_wire << " #" << key.id << "\n";
    }

    std::cout << "[CanvasLifecycleMemory] " << scope_wire << " #" << key.id << " has no active users, unloaded from pool (load -1)\n";
    return true;
}

bool CanvasLifecycleMemory::removeCanvas(int canvas_id) {
    return removeCanvas(SpaceScope::Canvas, canvas_id);
}

bool CanvasLifecycleMemory::removeCanvas(SpaceScope scope, int space_id) {
    return removeCanvasImpl({scope, space_id});
}

bool CanvasLifecycleMemory::removeCanvasImpl(SpaceKey key) {
    auto lifecycle_mtx = lifecycleMutexForSpace(key);
    std::lock_guard<std::mutex> lifecycle_lock(*lifecycle_mtx);

    std::shared_ptr<Canvas> canvas;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = canvases_.find(key);
        if (it != canvases_.end()) {
            canvas = it->second;
        }
    }

    if (!canvas) return false;

    if (canvas->hasRecentQuery()) return false;

    // Keep the shared instance discoverable while unload marks it as closing
    // and drains persistence.
    if (!unloadCanvas(key, canvas)) {
        std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
        canvas->unloading = false;
        return false;
    }

    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = canvases_.find(key);
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
    for (const auto& [key, canvas] : canvases_) {
        (void)canvas;
        if (key.scope == SpaceScope::Canvas) ids.push_back(key.id);
    }
    return ids;
}

std::vector<SpaceKey> CanvasLifecycleMemory::getActiveSpaceKeys() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    std::vector<SpaceKey> keys;
    keys.reserve(canvases_.size());
    for (const auto& [key, canvas] : canvases_) {
        (void)canvas;
        keys.push_back(key);
    }
    return keys;
}

void CanvasLifecycleMemory::cleanupInactiveCanvases() {
    std::vector<SpaceKey> active_keys = getActiveSpaceKeys();
    for (const auto& key : active_keys) {
        if (removeCanvasImpl(key)) {
            std::cout << "[CanvasLifecycleMemory] Cleanup task found no active users for "
                      << spaceScopeWire(key.scope) << " #" << key.id << ". Unloading.\n";
        }
    }
}


void CanvasLifecycleMemory::flushCanvasActivity(){
 for(const auto id:getActiveCanvasIds())try{
  const auto canvas=getCanvas(id);if(!canvas||canvas->unloading)continue;
  CanvasServiceMemory memory(canvas->getRedisIp(),canvas->getRedisPort());const auto clients=memory.brokerActivityClients(id);
  if(!clients){std::cerr<<"[CanvasActivity] Redis counters unavailable\n";continue;}
  RegistryServiceMemory registry(db_host_,db_port_);if(!registry.storeCanvasActivity(id,clients->dump()))std::cerr<<"[CanvasActivity] SQL flush failed; will retry next interval\n";
 }catch(const std::exception&){std::cerr<<"[CanvasActivity] Flush failed; durable counters retained\n";}
}
