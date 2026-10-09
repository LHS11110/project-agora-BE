#pragma once
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

// Opaque values and keys only. Admission, dirty state, and persistence belong
// to service memory. Returned values stay alive after eviction.
template <typename Value>
class LruMemory final {
public:
    explicit LruMemory(std::size_t capacity) : capacity_(capacity) {}
    std::shared_ptr<const Value> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = values_.find(key);
        if (found == values_.end()) return {};
        order_.splice(order_.begin(), order_, found->second.position);
        return found->second.value;
    }
    void add(const std::string& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!capacity_) return;
        auto replacement = std::make_shared<const Value>(value);
        auto found = values_.find(key);
        if (found != values_.end()) {
            found->second.value = std::move(replacement);
            order_.splice(order_.begin(), order_, found->second.position);
            return;
        }
        order_.push_front(key);
        try { values_.emplace(key, Entry{std::move(replacement), order_.begin()}); }
        catch (...) { order_.pop_front(); throw; }
        if (values_.size() > capacity_) {
            values_.erase(order_.back()); order_.pop_back();
        }
    }
    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = values_.find(key);
        if (found == values_.end()) return;
        order_.erase(found->second.position); values_.erase(found);
    }
private:
    struct Entry { std::shared_ptr<const Value> value; std::list<std::string>::iterator position; };
    const std::size_t capacity_;
    std::mutex mutex_;
    std::list<std::string> order_;
    std::unordered_map<std::string, Entry> values_;
};
