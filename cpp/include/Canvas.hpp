#pragma once

#include <string>
#include <chrono>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <nlohmann/json.hpp>

class Canvas {
public:

    Canvas(int canvas_id, const std::string& redis_ip = "127.0.0.1", int redis_port = 6379);
    ~Canvas();

    int getCanvasId() const { return canvas_id; }
    std::string getCanvasName() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return canvas_name;
    }
    void setCanvasName(const std::string& name) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        canvas_name = name;
    }
    long long getSettingsRevision() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return settings_revision_;
    }
    void setSettingsRevision(long long revision) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        settings_revision_ = revision;
    }
    int getAdminUserId() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return admin_user_id;
    }
    void setAdminUserId(int id) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        admin_user_id = id;
    }

    std::string getRedisIp() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return redis_ip;
    }
    int getRedisPort() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return redis_port;
    }
    void setRedisConfig(const std::string& ip, int port) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        redis_ip = ip;
        redis_port = port;
    }

    void touchQuery() { last_query_.store(std::chrono::steady_clock::now().time_since_epoch().count()); }
    bool hasRecentQuery(std::chrono::seconds ttl = std::chrono::seconds(90)) const {
        const auto elapsed = std::chrono::steady_clock::now().time_since_epoch().count() - last_query_.load();
        return elapsed < std::chrono::duration_cast<std::chrono::steady_clock::duration>(ttl).count();
    }
    int canvas_id;
    std::string canvas_name;
    int admin_user_id{0};
    std::string redis_ip;
    int redis_port;

    // Serializes settings mutations with the final Redis -> Elasticsearch flush.
    std::mutex settings_mutex;
    std::atomic<bool> unloading{false};

    // Persistence queue bookkeeping has a separate short-held mutex so event
    // loop enqueue never waits on settings/Redis/SQL/ES work. Tickets let
    // readers wait only for writes already accepted before their request.
    // Unload marks the canvas as closing and waits for all reserved Redis
    // writes before taking the final Redis -> Elasticsearch snapshot.
    bool enqueuePersistence(const nlohmann::json& event, bool& start_worker,
                            const std::string& request_id = {},
                            const std::string& parent_request_id = {});
    std::uint64_t persistenceBarrier();
    bool nextPersistence(nlohmann::json& event, std::uint64_t& ticket,
                         std::string* request_id = nullptr,
                         std::string* parent_request_id = nullptr);
    void cancelPersistenceQueue();
    void endPersistence(std::uint64_t ticket, bool succeeded);
    void waitForPersistenceThrough(std::uint64_t ticket);
    bool waitForPendingPersistence(std::unique_lock<std::mutex>& lock);

private:
    struct PersistenceEntry {
        std::uint64_t ticket;
        std::size_t bytes;
        nlohmann::json event;
        std::string request_id;
        std::string parent_request_id;
    };

    mutable std::mutex metadata_mutex_;
    long long settings_revision_{0};
    std::atomic<std::chrono::steady_clock::duration::rep> last_query_{std::chrono::steady_clock::now().time_since_epoch().count()};
    std::mutex persistence_mutex_;
    std::condition_variable persistence_cv_;
    std::size_t pending_persistence_{0};
    std::size_t queued_persistence_bytes_{0};
    std::uint64_t last_enqueued_persistence_{0};
    std::uint64_t last_completed_persistence_{0};
    std::deque<PersistenceEntry> persistence_queue_;
    bool persistence_worker_running_{false};
    bool persistence_failed_{false};
};
