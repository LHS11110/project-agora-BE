#include "Canvas.hpp"
#include <iostream>
#include <algorithm>
#include <limits>

Canvas::Canvas(int canvas_id, const std::string& redis_ip, int redis_port)
    : canvas_id(canvas_id), redis_ip(redis_ip), redis_port(redis_port) {
}

Canvas::~Canvas() = default;

bool Canvas::enqueuePersistence(const nlohmann::json& event, bool& start_worker,
                                const std::string& request_id,
                                const std::string& parent_request_id) {
    // Bound outstanding work before admitting a ticket. Serialization is outside
    // the queue mutex so workers can drain while a large event is measured.
    start_worker = false;
    const auto bytes = event.dump().size() + request_id.size() + parent_request_id.size();
    constexpr std::size_t max_bytes = 64 * 1024 * 1024;
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    if (pending_persistence_ >= 1024 || bytes > max_bytes
        || queued_persistence_bytes_ > max_bytes - bytes) return false;
    if (unloading.load() || persistence_failed_) return false;
    if (last_enqueued_persistence_ == std::numeric_limits<std::uint64_t>::max()) return false;
    const auto ticket = ++last_enqueued_persistence_;
    persistence_queue_.push_back({ticket, bytes, event, request_id, parent_request_id});
    queued_persistence_bytes_ += bytes;
    ++pending_persistence_;
    start_worker = !persistence_worker_running_;
    persistence_worker_running_ = true;
    return true;
}

std::uint64_t Canvas::persistenceBarrier() {
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    return last_enqueued_persistence_;
}

bool Canvas::nextPersistence(nlohmann::json& event, std::uint64_t& ticket,
                             std::string* request_id, std::string* parent_request_id) {
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    if (persistence_queue_.empty()) {
        persistence_worker_running_ = false;
        return false;
    }
    ticket = persistence_queue_.front().ticket;
    event = std::move(persistence_queue_.front().event);
    if (request_id) *request_id = std::move(persistence_queue_.front().request_id);
    if (parent_request_id) *parent_request_id = std::move(persistence_queue_.front().parent_request_id);
    queued_persistence_bytes_ -= persistence_queue_.front().bytes;
    persistence_queue_.pop_front();
    return true;
}

void Canvas::cancelPersistenceQueue() {
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    persistence_failed_ = true;
    if (pending_persistence_ >= persistence_queue_.size()) {
        pending_persistence_ -= persistence_queue_.size();
    } else {
        pending_persistence_ = 0;
    }
    persistence_queue_.clear();
    queued_persistence_bytes_ = 0;
    persistence_worker_running_ = false;
    if (pending_persistence_ == 0) {
        last_completed_persistence_ = last_enqueued_persistence_;
        persistence_cv_.notify_all();
    }
}

void Canvas::endPersistence(std::uint64_t ticket, bool succeeded) {
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    if (!succeeded) persistence_failed_ = true;
    if (pending_persistence_ > 0) --pending_persistence_;
    last_completed_persistence_ = std::max(last_completed_persistence_, ticket);
    persistence_cv_.notify_all();
}

void Canvas::waitForPersistenceThrough(std::uint64_t ticket) {
    std::unique_lock<std::mutex> lock(persistence_mutex_);
    persistence_cv_.wait(lock, [this, ticket]() { return last_completed_persistence_ >= ticket; });
}

bool Canvas::waitForPendingPersistence(std::unique_lock<std::mutex>& lock) {
    (void)lock;
    std::unique_lock<std::mutex> persistence_lock(persistence_mutex_);
    unloading.store(true);
    const auto ticket = last_enqueued_persistence_;
    persistence_cv_.wait(persistence_lock, [this, ticket]() {
        return last_completed_persistence_ >= ticket;
    });
    return !persistence_failed_;
}
