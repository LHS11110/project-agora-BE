#include "Canvas.hpp"
#include <iostream>
#include <algorithm>
#include <limits>

Canvas::Canvas(int canvas_id, const std::string& redis_ip, int redis_port)
    : canvas_id(canvas_id), redis_ip(redis_ip), redis_port(redis_port) {
}

Canvas::~Canvas() {
    // Explicit pool teardown is responsible for notifying the WebSocket
    // server. A destructor can run later on a persistence worker after the
    // canvas has already left the pool; calling the saved callback here would
    // risk invoking a WebSocketServer that has already been destroyed.
    std::lock_guard<std::mutex> lock(canvas_mutex);
    user_conn_counts.clear();
    active_users.clear();
    web_socket_callbacks_ = {};
}

bool Canvas::enqueuePersistence(const nlohmann::json& event, bool& start_worker) {
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    if (unloading.load() || persistence_failed_) return false;
    if (last_enqueued_persistence_ == std::numeric_limits<std::uint64_t>::max()) return false;
    const auto ticket = ++last_enqueued_persistence_;
    persistence_queue_.emplace_back(ticket, event);
    ++pending_persistence_;
    start_worker = !persistence_worker_running_;
    persistence_worker_running_ = true;
    return true;
}

std::uint64_t Canvas::persistenceBarrier() {
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    return last_enqueued_persistence_;
}

bool Canvas::nextPersistence(nlohmann::json& event, std::uint64_t& ticket) {
    std::lock_guard<std::mutex> lock(persistence_mutex_);
    if (persistence_queue_.empty()) {
        persistence_worker_running_ = false;
        return false;
    }
    ticket = persistence_queue_.front().first;
    event = std::move(persistence_queue_.front().second);
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

void Canvas::connectUser(int user_id) {
    std::lock_guard<std::mutex> lock(canvas_mutex);

    user_conn_counts[user_id]++;
    active_users.insert(user_id);

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " connected (connections: " << user_conn_counts[user_id] << "). Total active users: " << active_users.size()
              << " (WebSocket)\n";
}

bool Canvas::disconnectUser(int user_id) {
    std::size_t remaining_users = 0;
    bool user_became_inactive = false;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);

        const bool was_active = active_users.count(user_id) > 0;
        auto c_it = user_conn_counts.find(user_id);
        if (c_it != user_conn_counts.end()) {
            c_it->second--;
            if (c_it->second <= 0) {
                user_conn_counts.erase(c_it);
                active_users.erase(user_id);
            }
        } else {
            active_users.erase(user_id);
        }

        user_became_inactive = was_active && active_users.count(user_id) == 0;
        remaining_users = active_users.size();
    }

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " disconnected. Remaining active users: " << remaining_users << "\n";
    return user_became_inactive;
}

void Canvas::disconnectUserCompletely(int user_id) {
    WebSocketCallbacks callbacks;
    std::size_t remaining_users = 0;
    bool was_active = false;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        was_active = active_users.erase(user_id) > 0;
        user_conn_counts.erase(user_id);

        callbacks = web_socket_callbacks_;
        remaining_users = active_users.size();
    }

    if (was_active && callbacks.disconnect_user) {
        callbacks.disconnect_user(canvas_id, user_id);
    }

    std::cout << "[Canvas #" << canvas_id << "] User #" << user_id
              << " fully disconnected. Remaining active users: " << remaining_users << "\n";
}

void Canvas::disconnectAll() {
    WebSocketCallbacks callbacks;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        user_conn_counts.clear();
        active_users.clear();
        callbacks = web_socket_callbacks_;
    }

    if (callbacks.disconnect_all) {
        callbacks.disconnect_all(canvas_id);
    }

    std::cout << "[Canvas #" << canvas_id << "] All users disconnected\n";
}

void Canvas::setWebSocketCallbacks(WebSocketCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(canvas_mutex);
    web_socket_callbacks_ = std::move(callbacks);
}

void Canvas::broadcast(const nlohmann::json& data, int exclude_user_id) {
    WebSocketCallbacks callbacks;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        callbacks = web_socket_callbacks_;
    }

    if (callbacks.broadcast) {
        callbacks.broadcast(canvas_id, data, exclude_user_id);
    }
}

void Canvas::sendToUser(int user_id, const nlohmann::json& data) {
    WebSocketCallbacks callbacks;

    {
        std::lock_guard<std::mutex> lock(canvas_mutex);
        callbacks = web_socket_callbacks_;
    }

    if (callbacks.send_to_user) {
        callbacks.send_to_user(canvas_id, user_id, data);
    }
}

bool Canvas::isUserActive(int user_id) {
    std::lock_guard<std::mutex> lock(canvas_mutex);
    return active_users.count(user_id) > 0;
}

std::set<int> Canvas::getActiveUsers() {
    std::lock_guard<std::mutex> lock(canvas_mutex);
    return active_users;
}
