#include "CanvasMemory.hpp"
#include "RedisClient.hpp"
#include <Poco/LRUCache.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <list>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr std::size_t kCanvasItemCacheDefaultCapacity = 64;
constexpr std::size_t kCanvasItemCacheMaximumCapacity = 4096;
constexpr std::size_t kCanvasCacheDefaultMaximumCanvases = 256;
constexpr std::size_t kCanvasCacheMaximumCanvases = 4096;
constexpr std::size_t kMaximumCanvasItemCacheBytes = 256 * 1024;
constexpr char kRedisErrorMarker = '\x01';

bool isCanvasDocumentKey(const std::string& key) {
    constexpr char prefix[] = "canvas:";
    if (key.compare(0, sizeof(prefix) - 1, prefix) != 0
        || key.size() == sizeof(prefix) - 1) return false;
    for (std::size_t index = sizeof(prefix) - 1; index < key.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(key[index]))) return false;
    }
    return true;
}

std::size_t configuredCacheCapacity(const char* variable, std::size_t default_capacity,
                                    std::size_t maximum_capacity, bool allow_zero) {
    const char* configured = std::getenv(variable);
    if (!configured || !*configured) return default_capacity;
    for (const char* digit = configured; *digit; ++digit) {
        if (!std::isdigit(static_cast<unsigned char>(*digit))) {
            return default_capacity;
        }
    }
    try {
        std::size_t parsed_length = 0;
        const auto parsed = std::stoull(configured, &parsed_length);
        if (parsed_length != std::string(configured).size() || (!allow_zero && parsed == 0)) {
            return default_capacity;
        }
        return std::min<std::size_t>(parsed, maximum_capacity);
    } catch (...) {
        return default_capacity;
    }
}

struct CanvasItemCacheLookup {
    std::optional<std::string> value;
    bool admit_on_miss{false};
};

using CanvasItemWriter = std::function<bool(const std::string&, const std::string&, const std::string&)>;

std::mutex& canvasDocumentMutex(const std::string& key);

class CanvasItemLru {
public:
    explicit CanvasItemLru(std::size_t capacity)
        : capacity_(capacity), entries_(capacity), admission_(capacity) {}

    CanvasItemCacheLookup lookup(const std::string& item_id) {
        if (capacity_ == 0) return {};
        if (const auto cached = entries_.get(item_id)) {
            touch(item_id);
            return {*cached, false};
        }
        if (const auto state = admission_.get(item_id)) {
            if (*state == kUncacheable) return {};
            admission_.remove(item_id);
            return {std::nullopt, true};
        }
        admission_.add(item_id, kFirstAccess);
        return {};
    }

    std::optional<std::string> get(const std::string& item_id) {
        if (capacity_ == 0) return std::nullopt;
        const auto cached = entries_.get(item_id);
        if (!cached) return std::nullopt;
        touch(item_id);
        return *cached;
    }

    bool add(const std::string& canvas_key, const std::string& item_id,
             const std::string& item_json, bool dirty, const CanvasItemWriter& writer) {
        if (capacity_ == 0) return true;
        const auto current = entries_.get(item_id);
        const bool already_cached = static_cast<bool>(current);
        if (already_cached) touch(item_id);
        if (item_json.size() > kMaximumCanvasItemCacheBytes) {
            if (already_cached && dirty && !writer(canvas_key, item_id, item_json)) return false;
            if (already_cached) erase(item_id);
            admission_.add(item_id, kUncacheable);
            return true;
        }
        if (!already_cached && lru_.size() >= capacity_) {
            const std::string victim = lru_.back();
            if (dirty_items_.count(victim)) {
                const auto victim_json = entries_.get(victim);
                if (!victim_json || !writer(canvas_key, victim, *victim_json)) return false;
            }
            erase(victim);
        }
        admission_.remove(item_id);
        entries_.add(item_id, item_json);
        touch(item_id);
        if (dirty) dirty_items_.insert(item_id);
        else dirty_items_.erase(item_id);
        return true;
    }

    void remove(const std::string& item_id) {
        erase(item_id);
        admission_.remove(item_id);
    }

    bool update(const std::string& canvas_key, const std::string& item_id,
                const std::string& item_json, const CanvasItemWriter& writer) {
        if (!entries_.get(item_id)) return false;
        return add(canvas_key, item_id, item_json, true, writer);
    }

    bool markClean(const std::string& item_id) {
        if (!entries_.get(item_id)) return false;
        touch(item_id);
        dirty_items_.erase(item_id);
        return true;
    }

    std::vector<std::pair<std::string, std::string>> dirtyItems() {
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(dirty_items_.size());
        for (const auto& item_id : dirty_items_) {
            const auto item = entries_.get(item_id);
            if (item) {
                touch(item_id);
                result.emplace_back(item_id, *item);
            }
        }
        return result;
    }

    bool flush(const std::string& canvas_key, const CanvasItemWriter& writer) {
        const auto pending = dirtyItems();
        for (const auto& [item_id, item_json] : pending) {
            if (!writer(canvas_key, item_id, item_json)) return false;
            dirty_items_.erase(item_id);
        }
        return true;
    }

private:
    static constexpr int kFirstAccess = 1;
    static constexpr int kUncacheable = 2;

    void touch(const std::string& item_id) {
        const auto found = lru_positions_.find(item_id);
        if (found != lru_positions_.end()) lru_.erase(found->second);
        lru_.push_front(item_id);
        lru_positions_[item_id] = lru_.begin();
    }

    void erase(const std::string& item_id) {
        entries_.remove(item_id);
        dirty_items_.erase(item_id);
        const auto found = lru_positions_.find(item_id);
        if (found != lru_positions_.end()) {
            lru_.erase(found->second);
            lru_positions_.erase(found);
        }
    }

    std::size_t capacity_;
    Poco::LRUCache<std::string, std::string> entries_;
    Poco::LRUCache<std::string, int> admission_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_positions_;
    std::unordered_set<std::string> dirty_items_;
};

class CanvasItemCacheManager {
public:
    CanvasItemCacheManager(std::size_t maximum_canvases, std::size_t items_per_canvas)
        : maximum_canvases_(maximum_canvases), maximum_items_per_canvas_(items_per_canvas) {}

    CanvasItemCacheLookup lookup(const std::string& canvas_key, const std::string& item_id,
                                 const CanvasItemWriter& writer) {
        if (!isCanvasDocumentKey(canvas_key) || maximum_items_per_canvas_ == 0) return {};
        const auto cache = cacheForLookup(canvas_key, writer);
        return cache ? cache->lookup(item_id) : CanvasItemCacheLookup{};
    }

    std::optional<std::string> get(const std::string& canvas_key, const std::string& item_id) {
        if (!isCanvasDocumentKey(canvas_key)) return std::nullopt;
        const auto cache = find(canvas_key);
        return cache ? cache->get(item_id) : std::nullopt;
    }

    bool admit(const std::string& canvas_key, const std::string& item_id,
               const std::string& item_json, const CanvasItemWriter& writer) {
        if (!isCanvasDocumentKey(canvas_key) || maximum_items_per_canvas_ == 0) return false;
        const auto cache = cacheForLookup(canvas_key, writer);
        return cache && cache->add(canvas_key, item_id, item_json, false, writer);
    }

    bool update(const std::string& canvas_key, const std::string& item_id,
                const std::string& item_json, const CanvasItemWriter& writer) {
        const auto cache = find(canvas_key);
        return cache && cache->update(canvas_key, item_id, item_json, writer);
    }

    bool markClean(const std::string& canvas_key, const std::string& item_id) {
        const auto cache = find(canvas_key);
        return cache && cache->markClean(item_id);
    }

    std::vector<std::pair<std::string, std::string>> dirtyItems(const std::string& canvas_key) {
        const auto cache = find(canvas_key);
        return cache ? cache->dirtyItems() : std::vector<std::pair<std::string, std::string>>{};
    }

    bool flush(const std::string& canvas_key, const CanvasItemWriter& writer) {
        const auto cache = find(canvas_key);
        return !cache || cache->flush(canvas_key, writer);
    }

    void removeItem(const std::string& canvas_key, const std::string& item_id) {
        if (!isCanvasDocumentKey(canvas_key)) return;
        const auto cache = find(canvas_key);
        if (cache) cache->remove(item_id);
    }

    void removeCanvas(const std::string& canvas_key) {
        if (!isCanvasDocumentKey(canvas_key)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = caches_.find(canvas_key);
        if (found == caches_.end()) return;
        caches_.erase(found);
        const auto position = lru_positions_.find(canvas_key);
        if (position != lru_positions_.end()) {
            lru_.erase(position->second);
            lru_positions_.erase(position);
        }
    }

private:
    std::shared_ptr<CanvasItemLru> find(const std::string& canvas_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = caches_.find(canvas_key);
        if (found == caches_.end()) return nullptr;
        touch(canvas_key);
        return found->second;
    }

    void touch(const std::string& canvas_key) {
        const auto found = lru_positions_.find(canvas_key);
        if (found != lru_positions_.end()) lru_.erase(found->second);
        lru_.push_front(canvas_key);
        lru_positions_[canvas_key] = lru_.begin();
    }

    std::shared_ptr<CanvasItemLru> cacheForLookup(const std::string& canvas_key,
                                                  const CanvasItemWriter& writer) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = caches_.find(canvas_key);
        if (found != caches_.end()) {
            touch(canvas_key);
            return found->second;
        }
        if (maximum_canvases_ == 0) return nullptr;
        if (caches_.size() >= maximum_canvases_) {
            bool evicted = false;
            for (auto candidate = lru_.rbegin(); candidate != lru_.rend(); ++candidate) {
                const auto victim = caches_.find(*candidate);
                if (victim == caches_.end()) continue;
                // Callers hold their own canvas stripe; never block on a
                // different stripe while holding the cache-manager mutex.
                std::unique_lock<std::mutex> victim_document_lock(
                    canvasDocumentMutex(*candidate), std::try_to_lock);
                if (!victim_document_lock.owns_lock()) continue;
                const auto victim_writer = [&writer, &candidate](const std::string&, const std::string& id,
                                                                 const std::string& value) {
                    return writer(*candidate, id, value);
                };
                if (!victim->second->flush(*candidate, victim_writer)) continue;
                const std::string victim_key = *candidate;
                caches_.erase(victim);
                const auto position = lru_positions_.find(victim_key);
                if (position != lru_positions_.end()) {
                    lru_.erase(position->second);
                    lru_positions_.erase(position);
                }
                evicted = true;
                break;
            }
            if (!evicted) return nullptr;
        }
        auto created = std::make_shared<CanvasItemLru>(maximum_items_per_canvas_);
        caches_[canvas_key] = created;
        touch(canvas_key);
        return created;
    }

    std::size_t maximum_canvases_;
    std::size_t maximum_items_per_canvas_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<CanvasItemLru>> caches_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_positions_;
};

CanvasItemCacheManager& canvasItemCache() {
    static CanvasItemCacheManager cache(
        configuredCacheCapacity("CPP_CANVAS_LRU_CAPACITY",
                                kCanvasCacheDefaultMaximumCanvases,
                                kCanvasCacheMaximumCanvases, false),
        configuredCacheCapacity("CPP_CANVAS_LRU_ITEMS_PER_CANVAS",
                                kCanvasItemCacheDefaultCapacity,
                                kCanvasItemCacheMaximumCapacity, true));
    return cache;
}

std::array<std::mutex, 64>& canvasDocumentMutexes() {
    // Fixed stripes avoid retaining one mutex per canvas ID forever.
    static std::array<std::mutex, 64> mutexes;
    return mutexes;
}

std::mutex& canvasDocumentMutex(const std::string& key) {
    auto& mutexes = canvasDocumentMutexes();
    return mutexes[std::hash<std::string>{}(key) % mutexes.size()];
}

std::unique_lock<std::mutex> lockCanvasDocument(const std::string& key) {
    if (!isCanvasDocumentKey(key)) return {};
    return std::unique_lock<std::mutex>(canvasDocumentMutex(key));
}

struct JsonPathSegment {
    bool is_array_index{false};
    std::string key;
    std::size_t index{0};
};

bool parseJsonPath(const std::string& path, std::vector<JsonPathSegment>& segments) {
    if (path.empty() || path[0] != '$') return false;
    std::size_t position = 1;
    while (position < path.size()) {
        if (path[position] == '.') {
            const std::size_t begin = ++position;
            while (position < path.size() && path[position] != '.' && path[position] != '[') ++position;
            if (position == begin) return false;
            segments.push_back(JsonPathSegment{false, path.substr(begin, position - begin), 0});
            continue;
        }

        if (path[position] != '[') return false;
        ++position;
        if (position < path.size() && path[position] == '"') {
            const std::size_t begin = position;
            bool escaped = false;
            bool closed = false;
            ++position; // Skip the opening quote; the scan looks for the closing quote.
            for (; position < path.size(); ++position) {
                const char character = path[position];
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    ++position;
                    closed = true;
                    break;
                }
            }
            if (!closed || position >= path.size() || path[position] != ']') return false;
            try {
                const auto key = nlohmann::json::parse(path.substr(begin, position - begin));
                if (!key.is_string()) return false;
                segments.push_back(JsonPathSegment{false, key.get<std::string>(), 0});
            } catch (...) {
                return false;
            }
            ++position;
            continue;
        }

        const std::size_t begin = position;
        while (position < path.size() && std::isdigit(static_cast<unsigned char>(path[position]))) ++position;
        if (position == begin || position >= path.size() || path[position] != ']') return false;
        try {
            std::size_t parsed_length = 0;
            const auto index = std::stoull(path.substr(begin, position - begin), &parsed_length);
            if (parsed_length != position - begin || index > std::numeric_limits<std::size_t>::max()) return false;
            segments.push_back(JsonPathSegment{true, {}, static_cast<std::size_t>(index)});
        } catch (...) {
            return false;
        }
        ++position;
    }
    return true;
}

struct CanvasItemPath {
    std::string item_id;
    std::vector<JsonPathSegment> relative_segments;
};

bool parseCanvasItemPath(const std::string& path, CanvasItemPath& item_path) {
    std::vector<JsonPathSegment> segments;
    if (!parseJsonPath(path, segments) || segments.size() < 2
        || segments[0].is_array_index || segments[0].key != "items"
        || segments[1].is_array_index) return false;

    item_path.item_id = segments[1].key;
    item_path.relative_segments.assign(segments.begin() + 2, segments.end());
    return true;
}

std::string canvasItemJsonPath(const std::string& item_id) {
    return "$[\"items\"][" + nlohmann::json(item_id).dump() + "]";
}

bool isChatRoomItem(const nlohmann::json& item) {
    return item.is_object() && item.contains("type") && item["type"].is_string()
        && item["type"].get<std::string>() == "chat_room";
}

bool isChatHistoryPath(const CanvasItemPath& item_path) {
    return !item_path.relative_segments.empty()
        && !item_path.relative_segments.front().is_array_index
        && item_path.relative_segments.front().key == "data";
}

nlohmann::json cacheableCanvasItem(nlohmann::json item) {
    if (isChatRoomItem(item)) item.erase("data");
    return item;
}

bool cachedCanvasItemCanAnswer(const nlohmann::json& item, const CanvasItemPath& path) {
    if (!isChatRoomItem(item)) return true;
    // Chat history is variable-size and is always read from Redis. The item
    // cache retains only room metadata such as its type and next sequence.
    return !path.relative_segments.empty() && !isChatHistoryPath(path);
}

bool findJsonPathValue(const nlohmann::json& root, const std::vector<JsonPathSegment>& segments,
                       const nlohmann::json*& value);

std::string jsonPathResult(const nlohmann::json& item,
                           const std::vector<JsonPathSegment>& relative_segments) {
    const nlohmann::json* value = nullptr;
    if (!findJsonPathValue(item, relative_segments, value)) return "[]";
    return nlohmann::json::array({*value}).dump();
}

std::optional<nlohmann::json> getCachedCanvasItem(
        const std::string& canvas_key, const std::string& item_id) {
    const auto cached = canvasItemCache().get(canvas_key, item_id);
    if (!cached) return std::nullopt;
    try {
        return nlohmann::json::parse(*cached);
    } catch (...) {
        canvasItemCache().removeItem(canvas_key, item_id);
        return std::nullopt;
    }
}

std::string overlayDirtyCanvasItems(const std::string& canvas_key, const std::string& document_json) {
    try {
        auto document = nlohmann::json::parse(document_json);
        if (!document.is_object() || !document.contains("items") || !document["items"].is_object()) {
            return document_json;
        }
        for (const auto& [item_id, item_json] : canvasItemCache().dirtyItems(canvas_key)) {
            auto item = nlohmann::json::parse(item_json);
            if (isChatRoomItem(item) && !item.contains("data")
                && document["items"].contains(item_id)
                && document["items"][item_id].is_object()
                && document["items"][item_id].contains("data")) {
                item["data"] = document["items"][item_id]["data"];
            }
            document["items"][item_id] = std::move(item);
        }
        return document.dump();
    } catch (...) {
        return document_json;
    }
}

void invalidateCanvasItemCacheForPath(const std::string& canvas_key, const std::string& path) {
    if (!isCanvasDocumentKey(canvas_key)) return;
    std::vector<JsonPathSegment> segments;
    if (!parseJsonPath(path, segments) || segments.empty()) {
        canvasItemCache().removeCanvas(canvas_key);
        return;
    }
    if (segments.front().is_array_index || segments.front().key != "items") return;
    if (segments.size() < 2 || segments[1].is_array_index) {
        canvasItemCache().removeCanvas(canvas_key);
        return;
    }
    canvasItemCache().removeItem(canvas_key, segments[1].key);
}

bool setJsonPathValue(nlohmann::json& root, const std::vector<JsonPathSegment>& segments,
                      const nlohmann::json& value) {
    if (segments.empty()) {
        root = value;
        return true;
    }

    nlohmann::json* parent = &root;
    for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        const auto& segment = segments[index];
        if (segment.is_array_index) {
            if (!parent->is_array() || segment.index >= parent->size()) return false;
            parent = &(*parent)[segment.index];
        } else {
            if (!parent->is_object() || !parent->contains(segment.key)) return false;
            parent = &(*parent)[segment.key];
        }
    }

    const auto& target = segments.back();
    if (target.is_array_index) {
        if (!parent->is_array() || target.index >= parent->size()) return false;
        (*parent)[target.index] = value;
    } else {
        if (!parent->is_object()) return false;
        (*parent)[target.key] = value;
    }
    return true;
}

bool findJsonPathValue(const nlohmann::json& root, const std::vector<JsonPathSegment>& segments,
                       const nlohmann::json*& value) {
    const nlohmann::json* current = &root;
    for (const auto& segment : segments) {
        if (segment.is_array_index) {
            if (!current->is_array() || segment.index >= current->size()) return false;
            current = &(*current)[segment.index];
        } else {
            if (!current->is_object() || !current->contains(segment.key)) return false;
            current = &(*current)[segment.key];
        }
    }
    value = current;
    return true;
}

bool deleteJsonPathValue(nlohmann::json& root, const std::vector<JsonPathSegment>& segments) {
    if (segments.empty()) return false; // Deleting '$' removes the Redis key itself.

    nlohmann::json* parent = &root;
    for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        const auto& segment = segments[index];
        if (segment.is_array_index) {
            if (!parent->is_array() || segment.index >= parent->size()) return true;
            parent = &(*parent)[segment.index];
        } else {
            if (!parent->is_object() || !parent->contains(segment.key)) return true;
            parent = &(*parent)[segment.key];
        }
    }

    const auto& target = segments.back();
    if (target.is_array_index) {
        if (!parent->is_array() || target.index >= parent->size()) return true;
        parent->erase(parent->begin() + static_cast<std::ptrdiff_t>(target.index));
    } else if (parent->is_object()) {
        parent->erase(target.key);
    }
    return true;
}

bool isWrongTypeResponse(const std::string& response) {
    const std::string message = !response.empty() && response.front() == kRedisErrorMarker
        ? response.substr(1) : response;
    std::string normalized = message;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized.find("wrongtype") != std::string::npos
        || normalized.find("wrong redis type") != std::string::npos;
}

bool isRedisErrorResponse(const std::string& response) {
    return !response.empty() && response.front() == kRedisErrorMarker;
}

}

struct CanvasMemory::Impl {
    explicit Impl(std::unique_ptr<RedisCommandExecutor> connection)
        : transport(std::move(connection)) {
        if (!transport) throw std::invalid_argument("CanvasMemory transport must not be null");
    }
    std::unique_ptr<RedisCommandExecutor> transport;
    std::optional<std::string> response;
};

CanvasMemory::CanvasMemory(const std::string& host, int port,
                           const std::string& user, const std::string& password)
    : CanvasMemory(std::make_unique<RedisClient>(host, port, user, password)) {}

CanvasMemory::CanvasMemory(std::unique_ptr<RedisCommandExecutor> transport)
    : impl_(std::make_unique<Impl>(std::move(transport))) {}

CanvasMemory::~CanvasMemory() = default;
bool CanvasMemory::connect() { return impl_->transport->connect(); }
void CanvasMemory::disconnect() { impl_->transport->disconnect(); impl_->response.reset(); }
bool CanvasMemory::ping() {
    const auto reply = impl_->transport->execute({"PING"});
    return reply && *reply == "PONG";
}

bool CanvasMemory::sendCommand(const std::vector<std::string>& command) {
    impl_->response = impl_->transport->execute(command);
    return impl_->response.has_value();
}

std::string CanvasMemory::readResponse() {
    auto response = std::move(impl_->response);
    impl_->response.reset();
    return response.value_or("");
}

bool CanvasMemory::set(const std::string& key, const std::string& value) {
    auto document_lock = lockCanvasDocument(key);
    const bool is_document = isCanvasDocumentKey(key);
    if (is_document && !flushCanvasItemCacheLocked(key)) return false;
    if (!sendCommand({"JSON.SET", key, "$", value})) {
        if (is_document) canvasItemCache().removeCanvas(key);
        return false;
    }
    std::string res = readResponse();
    if (isWrongTypeResponse(res)) {
        if (!sendCommand({"DEL", key})) {
            if (is_document) canvasItemCache().removeCanvas(key);
            return false;
        }
        const std::string deleted = readResponse();
        try {
            if (std::stoi(deleted) < 0) {
                if (is_document) canvasItemCache().removeCanvas(key);
                return false;
            }
        } catch (...) {
            if (is_document) canvasItemCache().removeCanvas(key);
            return false;
        }
        if (!sendCommand({"JSON.SET", key, "$", value})) {
            if (is_document) canvasItemCache().removeCanvas(key);
            return false;
        }
        res = readResponse();
    }
    const bool stored = res == "OK";
    if (is_document) canvasItemCache().removeCanvas(key);
    return stored;
}

std::optional<std::string> CanvasMemory::get(const std::string& key) {
    auto document_lock = lockCanvasDocument(key);
    auto document = readFromRedis(key);
    if (!document || !isCanvasDocumentKey(key)) return document;
    return overlayDirtyCanvasItems(key, *document);
}

bool CanvasMemory::flushCanvasItemCache(const std::string& key) {
    auto document_lock = lockCanvasDocument(key);
    if (!isCanvasDocumentKey(key)) return false;
    return flushCanvasItemCacheLocked(key);
}

bool CanvasMemory::flushCanvasItemCacheLocked(const std::string& key) {
    const CanvasItemWriter writer = [this](const std::string& canvas_key,
                                           const std::string& item_id,
                                           const std::string& item_json) {
        return writeBackCanvasItem(canvas_key, item_id, item_json);
    };
    return canvasItemCache().flush(key, writer);
}

std::optional<std::string> CanvasMemory::readFromRedis(const std::string& key) {
    if (!sendCommand({"JSON.GET", key})) return std::nullopt;
    std::string res = readResponse();
    if (isWrongTypeResponse(res)) {
        if (!sendCommand({"GET", key})) return std::nullopt;
        res = readResponse();
    }
    if (res.empty() || isRedisErrorResponse(res)) return std::nullopt;
    return res;
}

std::optional<std::string> CanvasMemory::readJsonPathFromRedis(
        const std::string& key, const std::string& path) {
    if (!sendCommand({"JSON.GET", key, path})) return std::nullopt;
    const std::string response = readResponse();
    if (response.empty() || isRedisErrorResponse(response)) return std::nullopt;
    return response;
}

bool CanvasMemory::writeBackCanvasItem(const std::string& key, const std::string& item_id,
                                     const std::string& item_json) {
    nlohmann::json item;
    try {
        item = nlohmann::json::parse(item_json);
    } catch (...) {
        return false;
    }
    if (isChatRoomItem(item) && !item.contains("data")) {
        const auto current = readJsonPathFromRedis(key, canvasItemJsonPath(item_id));
        if (!current) return false;
        try {
            const auto matches = nlohmann::json::parse(*current);
            if (!matches.is_array() || matches.empty() || !matches.front().is_object()) return false;
            if (matches.front().contains("data")) item["data"] = matches.front()["data"];
        } catch (...) {
            return false;
        }
    }
    if (!sendCommand({"JSON.SET", key, canvasItemJsonPath(item_id), item.dump()})) return false;
    return readResponse() == "OK";
}

std::optional<std::string> CanvasMemory::getJsonPath(const std::string& key, const std::string& path) {
    auto document_lock = lockCanvasDocument(key);
    CanvasItemPath item_path;
    if (isCanvasDocumentKey(key) && parseCanvasItemPath(path, item_path)) {
        const CanvasItemWriter writer = [this](const std::string& canvas_key,
                                               const std::string& item_id,
                                               const std::string& item_json) {
            return writeBackCanvasItem(canvas_key, item_id, item_json);
        };
        const auto lookup = canvasItemCache().lookup(key, item_path.item_id, writer);
        if (lookup.value) {
            try {
                const auto cached_item = nlohmann::json::parse(*lookup.value);
                if (cachedCanvasItemCanAnswer(cached_item, item_path)) {
                    const auto result = jsonPathResult(cached_item, item_path.relative_segments);
                    if (result != "[]" || !isChatRoomItem(cached_item)) return result;
                } else if (isChatRoomItem(cached_item)
                           && item_path.relative_segments.empty()) {
                    const auto raw_item = readJsonPathFromRedis(
                        key, canvasItemJsonPath(item_path.item_id));
                    if (raw_item) {
                        try {
                            auto matches = nlohmann::json::parse(*raw_item);
                            if (matches.is_array() && !matches.empty() && matches.front().is_object()) {
                                auto merged = cached_item;
                                if (matches.front().contains("data")) {
                                    merged["data"] = matches.front()["data"];
                                }
                                return nlohmann::json::array({merged}).dump();
                            }
                        } catch (...) {}
                    }
                }
            } catch (...) {
                canvasItemCache().removeItem(key, item_path.item_id);
            }
        } else if (lookup.admit_on_miss && isChatHistoryPath(item_path)) {
            // History can grow without bound, so repeated history reads still
            // use Redis range/path queries instead of promoting it as an item.
            return readJsonPathFromRedis(key, path);
        } else if (lookup.admit_on_miss) {
            const auto raw_item = readJsonPathFromRedis(key, canvasItemJsonPath(item_path.item_id));
            if (!raw_item) return std::nullopt;
            try {
                const auto matches = nlohmann::json::parse(*raw_item);
                if (!matches.is_array()) return readJsonPathFromRedis(key, path);
                if (matches.empty()) return std::string("[]");

                const auto& item = matches.front();
                canvasItemCache().admit(key, item_path.item_id,
                                        cacheableCanvasItem(item).dump(), writer);
                if (cachedCanvasItemCanAnswer(item, item_path)) {
                    return jsonPathResult(item, item_path.relative_segments);
                }
                // A chat-room root read includes history for this response, but
                // only its small metadata is admitted to the item cache.
                if (item_path.relative_segments.empty()) return *raw_item;
                return jsonPathResult(item, item_path.relative_segments);
            } catch (...) {
                return readJsonPathFromRedis(key, path);
            }
        }
    } else if (isCanvasDocumentKey(key)) {
        std::vector<JsonPathSegment> segments;
        if (parseJsonPath(path, segments)
            && (segments.empty() || (segments.size() == 1 && !segments[0].is_array_index
                                      && segments[0].key == "items"))) {
            auto document = readFromRedis(key);
            if (!document) return std::nullopt;
            try {
                const auto merged = nlohmann::json::parse(overlayDirtyCanvasItems(key, *document));
                return jsonPathResult(merged, segments);
            } catch (...) {
                return readJsonPathFromRedis(key, path);
            }
        }
    }
    return readJsonPathFromRedis(key, path);
}

bool CanvasMemory::setJsonPath(const std::string& key, const std::string& path, const nlohmann::json& value) {
    auto document_lock = lockCanvasDocument(key);
    const bool is_document = isCanvasDocumentKey(key);
    CanvasItemPath item_path;
    const bool is_item_path = is_document && parseCanvasItemPath(path, item_path);
    const CanvasItemWriter writer = [this](const std::string& canvas_key,
                                           const std::string& item_id,
                                           const std::string& item_json) {
        return writeBackCanvasItem(canvas_key, item_id, item_json);
    };

    if (is_item_path && !isChatHistoryPath(item_path)) {
        auto cached_item = getCachedCanvasItem(key, item_path.item_id);
        if (cached_item) {
            nlohmann::json updated = *cached_item;
            if (item_path.relative_segments.empty()) {
                updated = value;
                if (isChatRoomItem(*cached_item) && isChatRoomItem(updated)
                    && !updated.contains("data")) {
                    // A cached chat room omits its growing history array. Keep
                    // the Redis copy of that field when a whole room is replaced.
                    const auto current = readJsonPathFromRedis(key, canvasItemJsonPath(item_path.item_id));
                    if (current) {
                        try {
                            const auto matches = nlohmann::json::parse(*current);
                            if (matches.is_array() && !matches.empty()
                                && matches.front().is_object() && matches.front().contains("data")) {
                                updated["data"] = matches.front()["data"];
                            }
                        } catch (...) {}
                    }
                }
            } else if (!setJsonPathValue(updated, item_path.relative_segments, value)) {
                return false;
            }
            updated = cacheableCanvasItem(std::move(updated));
            return canvasItemCache().update(key, item_path.item_id, updated.dump(), writer);
        }
        // New items are created in Redis only. Reads may promote them later.
    } else if (is_document && !is_item_path) {
        std::vector<JsonPathSegment> segments;
        const bool parsed = parseJsonPath(path, segments);
        const bool replaces_items = !parsed || segments.empty()
            || (!segments.front().is_array_index && segments.front().key == "items"
                && (segments.size() < 2 || segments[1].is_array_index));
        if (replaces_items && !flushCanvasItemCacheLocked(key)) return false;
    }

    if (!sendCommand({"JSON.SET", key, path, value.dump()})) {
        if (is_document && !is_item_path) invalidateCanvasItemCacheForPath(key, path);
        return false;
    }
    if (readResponse() != "OK") {
        if (is_document && !is_item_path) invalidateCanvasItemCacheForPath(key, path);
        return false;
    }
    if (is_document && !is_item_path) invalidateCanvasItemCacheForPath(key, path);
    return true;
}

bool CanvasMemory::appendChatMessage(const std::string& key, const std::string& item_id,
                                   std::uint64_t sequence, const nlohmann::json& message) {
    auto document_lock = lockCanvasDocument(key);
    const bool is_document = isCanvasDocumentKey(key);
    if (is_document && !flushCanvasItemCacheLocked(key)) return false;
    bool cache_updated = false;
    if (is_document) {
        auto room = getCachedCanvasItem(key, item_id);
        if (room && isChatRoomItem(*room)
            && sequence < std::numeric_limits<std::uint64_t>::max()) {
            (*room)["next_sequence"] = sequence + 1;
            const CanvasItemWriter writer = [this](const std::string& canvas_key,
                                                   const std::string& cached_item_id,
                                                   const std::string& item_json) {
                return writeBackCanvasItem(canvas_key, cached_item_id, item_json);
            };
            cache_updated = canvasItemCache().update(
                key, item_id, cacheableCanvasItem(*room).dump(), writer);
        } else if (room) {
            canvasItemCache().removeItem(key, item_id);
        }
    }

    const std::string room_path = canvasItemJsonPath(item_id);
    const std::string type_path = room_path + "[\"type\"]";
    const std::string data_path = room_path + "[\"data\"]";
    const std::string sequence_path = room_path + "[\"next_sequence\"]";
    static const std::string script = R"LUA(
local type_raw = redis.call('JSON.GET', KEYS[1], ARGV[1])
if not type_raw then return 'MISSING_ROOM' end
local types = cjson.decode(type_raw)
if types[1] ~= 'chat_room' then return 'NOT_CHAT_ROOM' end

local lengths_ok, lengths = pcall(redis.call, 'JSON.ARRLEN', KEYS[1], ARGV[2])
if not lengths_ok then return 'BAD_HISTORY' end
local length_value = type(lengths) == 'table' and lengths[1] or lengths
local data_length = tonumber(length_value)
if not data_length then
    redis.call('JSON.SET', KEYS[1], ARGV[2], '[]')
    data_length = 0
end

local next_raw = redis.call('JSON.GET', KEYS[1], ARGV[3])
local next_sequence = nil
if next_raw then
    local next_matches = cjson.decode(next_raw)
    next_sequence = tonumber(next_matches[1])
end
if not next_sequence then
    local history_raw = redis.call('JSON.GET', KEYS[1], ARGV[2])
    local maximum = 0
    if history_raw then
        local history_matches = cjson.decode(history_raw)
        local history = history_matches[1]
        if type(history) ~= 'table' then return 'BAD_HISTORY' end
        for _, previous in ipairs(history) do
            if type(previous) == 'table' then
                local previous_sequence = tonumber(previous.sequence)
                if previous_sequence and previous_sequence > maximum then maximum = previous_sequence end
            end
        end
    end
    next_sequence = maximum + 1
end
local expected = tonumber(ARGV[4])
if next_sequence ~= expected then return 'SEQUENCE_CONFLICT' end

local message = cjson.decode(ARGV[5])
if tonumber(message.sequence) ~= expected then return 'BAD_MESSAGE' end
local appended = redis.call('JSON.ARRAPPEND', KEYS[1], ARGV[2], ARGV[5])
if not appended then return 'APPEND_FAILED' end
redis.call('JSON.SET', KEYS[1], ARGV[3], tostring(expected + 1))
return 'OK'
)LUA";
    if (!sendCommand({"EVAL", script, "1", key, type_path, data_path, sequence_path,
                      std::to_string(sequence), message.dump()})) {
        if (is_document) canvasItemCache().removeItem(key, item_id);
        return false;
    }
    const bool stored = readResponse() == "OK";
    if ((!stored || !cache_updated) && is_document) canvasItemCache().removeItem(key, item_id);
    if (stored && is_document && cache_updated) canvasItemCache().markClean(key, item_id);
    return stored;
}

std::optional<std::string> CanvasMemory::getChatHistoryPage(
        const std::string& key, const std::string& item_id,
        const std::optional<std::uint64_t>& from_sequence,
        const std::optional<std::uint64_t>& to_sequence, std::uint64_t limit) {
    std::string escaped_id;
    escaped_id.reserve(item_id.size());
    for (char ch : item_id) {
        if (ch == '\\' || ch == '"') escaped_id.push_back('\\');
        escaped_id.push_back(ch);
    }
    const std::string room_path = "$[\"items\"][\"" + escaped_id + "\"]";
    const std::string type_path = room_path + "[\"type\"]";
    const std::string data_path = room_path + "[\"data\"]";
    const std::string mode = from_sequence && to_sequence ? "range"
        : from_sequence ? "from" : to_sequence ? "to" : "latest";
    static const std::string script = R"LUA(
local type_raw = redis.call('JSON.GET', KEYS[1], ARGV[2])
if not type_raw then return 'ROOM_NOT_FOUND' end
local types = cjson.decode(type_raw)
if types[1] ~= 'chat_room' then return 'ROOM_NOT_FOUND' end

local lengths_ok, lengths = pcall(redis.call, 'JSON.ARRLEN', KEYS[1], ARGV[3])
if not lengths_ok then return 'BAD_HISTORY' end
local length_value = type(lengths) == 'table' and lengths[1] or lengths
local total = tonumber(length_value) or 0
local requested_from = tonumber(ARGV[4]) or 0
local requested_to = tonumber(ARGV[5]) or 0
local requested_limit = tonumber(ARGV[6]) or 50
local mode = ARGV[7]
local start_index = 0
local end_index = total
local has_more = false

if mode == 'latest' then
    start_index = math.max(0, total - requested_limit)
    has_more = start_index > 0
elseif mode == 'from' then
    start_index = math.min(total, requested_from - 1)
    end_index = math.min(total, start_index + requested_limit)
    has_more = end_index < total
elseif mode == 'to' then
    end_index = math.min(total, requested_to)
    start_index = math.max(0, end_index - requested_limit)
    has_more = start_index > 0
else
    start_index = math.min(total, requested_from - 1)
    end_index = math.min(total, requested_to)
    if end_index < start_index then end_index = start_index end
end

local slice_path = ARGV[3] .. '[' .. start_index .. ':' .. end_index .. ']'
local slice_ok, slice_raw = pcall(redis.call, 'JSON.GET', KEYS[1], slice_path)
if not slice_ok then slice_raw = nil end
local messages = slice_raw and cjson.decode(slice_raw) or {}
local contiguous = type(messages) == 'table' and #messages == end_index - start_index
if contiguous then
    for i, message in ipairs(messages) do
        if type(message) ~= 'table' or tonumber(message.sequence) ~= start_index + i then
            contiguous = false
            break
        end
    end
end

if not contiguous then
    local raw = redis.call('JSON.GET', KEYS[1], ARGV[1])
    if not raw then return 'ROOM_NOT_FOUND' end
    local matches = cjson.decode(raw)
    local room = matches[1]
    local data = room.data or {}
    if type(data) ~= 'table' then return 'BAD_HISTORY' end
    local rows = {}
    for _, message in ipairs(data) do
        if type(message) == 'table' then
            local sequence = tonumber(message.sequence)
            if sequence and sequence > 0 then
                rows[#rows + 1] = {sequence = sequence, message = message}
            end
        end
    end
    table.sort(rows, function(left, right) return left.sequence < right.sequence end)
    local selected = {}
    total = #rows
    if mode == 'latest' then
        local first = math.max(1, #rows - requested_limit + 1)
        has_more = first > 1
        for i = first, #rows do selected[#selected + 1] = rows[i] end
    elseif mode == 'from' then
        local candidates = {}
        for _, row in ipairs(rows) do
            if row.sequence >= requested_from then candidates[#candidates + 1] = row end
        end
        local count = math.min(#candidates, requested_limit)
        has_more = #candidates > count
        for i = 1, count do selected[#selected + 1] = candidates[i] end
    elseif mode == 'to' then
        local candidates = {}
        for _, row in ipairs(rows) do
            if row.sequence <= requested_to then candidates[#candidates + 1] = row end
        end
        local first = math.max(1, #candidates - requested_limit + 1)
        has_more = first > 1
        for i = first, #candidates do selected[#selected + 1] = candidates[i] end
    else
        for _, row in ipairs(rows) do
            if row.sequence >= requested_from and row.sequence <= requested_to then
                selected[#selected + 1] = row
            end
        end
    end
    messages = {}
    for _, row in ipairs(selected) do messages[#messages + 1] = row.message end
    start_index = selected[1] and selected[1].sequence - 1 or 0
    end_index = selected[#selected] and selected[#selected].sequence or 0
end

local response = {total = total, messages = messages, has_more = has_more}
if #messages > 0 then
    response.from_sequence = tonumber(messages[1].sequence) or start_index + 1
    response.to_sequence = tonumber(messages[#messages].sequence) or end_index
end
    return cjson.encode(response)
)LUA";
    if (!sendCommand({"EVAL", script, "1", key, room_path, type_path, data_path,
                      from_sequence ? std::to_string(*from_sequence) : std::string("0"),
                      to_sequence ? std::to_string(*to_sequence) : std::string("0"),
                      std::to_string(limit), mode})) return std::nullopt;
    const std::string response = readResponse();
    if (response.empty() || isRedisErrorResponse(response)) return std::nullopt;
    return response;
}

CanvasMemory::CompareSetResult CanvasMemory::compareAndSetJsonPaths(
        const std::string& key, long long expected_revision,
        const std::vector<std::pair<std::string, nlohmann::json>>& values,
        const std::vector<std::string>& deletes) {
    if (values.empty() && deletes.empty()) return CompareSetResult::Error;
    auto document_lock = lockCanvasDocument(key);
    const bool is_document = isCanvasDocumentKey(key);
    const auto touches_items = [](const std::string& path) {
        std::vector<JsonPathSegment> segments;
        if (!parseJsonPath(path, segments) || segments.empty()) return true;
        return !segments.front().is_array_index && segments.front().key == "items";
    };
    if (is_document) {
        const bool item_paths = std::any_of(values.begin(), values.end(), [&](const auto& entry) {
            return touches_items(entry.first);
        }) || std::any_of(deletes.begin(), deletes.end(), touches_items);
        if (item_paths && !flushCanvasItemCacheLocked(key)) return CompareSetResult::Error;
    }
    const auto invalidate_changed_paths = [&]() {
        if (!is_document) return;
        for (const auto& [path, value] : values) {
            (void)value;
            invalidateCanvasItemCacheForPath(key, path);
        }
        for (const auto& path : deletes) invalidateCanvasItemCacheForPath(key, path);
    };
    static const std::string script = R"LUA(
local raw = redis.call('JSON.GET', KEYS[1], '$["settings-revision"]')
if redis.call('EXISTS', KEYS[1]) == 0 then return 'MISSING' end
local current = 0
if raw then
    local versions = cjson.decode(raw)
    current = tonumber(versions[1] or 0)
end
if current ~= tonumber(ARGV[1]) then return 'CONFLICT' end
local value_count = tonumber(ARGV[2])
local next_arg = 3
for i = next_arg, next_arg + value_count * 2 - 1, 2 do
    redis.call('JSON.SET', KEYS[1], ARGV[i], ARGV[i + 1])
end
local delete_count_index = next_arg + value_count * 2
local delete_count = tonumber(ARGV[delete_count_index])
for i = delete_count_index + 1, delete_count_index + delete_count do
    redis.call('JSON.DEL', KEYS[1], ARGV[i])
end
return 'OK'
)LUA";
    std::vector<std::string> command = {"EVAL", script, "1", key, std::to_string(expected_revision),
                                        std::to_string(values.size())};
    for (const auto& [path, value] : values) {
        command.push_back(path);
        command.push_back(value.dump());
    }
    command.push_back(std::to_string(deletes.size()));
    for (const auto& path : deletes) command.push_back(path);
    if (!sendCommand(command)) {
        invalidate_changed_paths();
        return CompareSetResult::Error;
    }
    const std::string result = readResponse();
    if (result == "OK") {
        invalidate_changed_paths();
        return CompareSetResult::Applied;
    }
    if (result == "CONFLICT") return CompareSetResult::Conflict;
    invalidate_changed_paths();
    if (is_document && result == "MISSING") canvasItemCache().removeCanvas(key);
    return CompareSetResult::Error;
}

bool CanvasMemory::deleteJsonPath(const std::string& key, const std::string& path) {
    auto document_lock = lockCanvasDocument(key);
    const bool is_document = isCanvasDocumentKey(key);
    CanvasItemPath item_path;
    const bool is_item_path = is_document && parseCanvasItemPath(path, item_path);
    if (is_item_path && !item_path.relative_segments.empty()
        && !isChatHistoryPath(item_path)) {
        if (auto cached_item = getCachedCanvasItem(key, item_path.item_id)) {
            if (deleteJsonPathValue(*cached_item, item_path.relative_segments)) {
                const CanvasItemWriter writer = [this](const std::string& canvas_key,
                                                       const std::string& item_id,
                                                       const std::string& item_json) {
                    return writeBackCanvasItem(canvas_key, item_id, item_json);
                };
                if (!canvasItemCache().update(
                        key, item_path.item_id, cacheableCanvasItem(*cached_item).dump(), writer)) {
                    return false;
                }
                if (!writeBackCanvasItem(key, item_path.item_id,
                                         cacheableCanvasItem(*cached_item).dump())) return false;
                canvasItemCache().markClean(key, item_path.item_id);
                return true;
            }
            // The cached shape may omit fields that live in Redis, such as
            // chat history. If Redis handles the delete, reload on next access.
        }
    }

    const bool deletes_all_items = is_document && !is_item_path && [&]() {
        std::vector<JsonPathSegment> segments;
        return parseJsonPath(path, segments) && segments.size() == 1
            && !segments.front().is_array_index && segments.front().key == "items";
    }();
    if (!sendCommand({"JSON.DEL", key, path})) return false;
    const std::string response = readResponse();
    const bool deleted = !response.empty() && !isRedisErrorResponse(response);
    if (deleted && is_document) {
        if (deletes_all_items) {
            canvasItemCache().removeCanvas(key);
        } else if (is_item_path && item_path.relative_segments.empty()) {
            canvasItemCache().removeItem(key, item_path.item_id);
        } else if (is_item_path && !isChatHistoryPath(item_path)
                   && getCachedCanvasItem(key, item_path.item_id)) {
            canvasItemCache().removeItem(key, item_path.item_id);
        }
    }
    return deleted;
}

bool CanvasMemory::del(const std::string& key) {
    auto document_lock = lockCanvasDocument(key);
    if (!sendCommand({"DEL", key})) return false;
    const std::string response = readResponse();
    try {
        const bool deleted = std::stoi(response) >= 0;
        if (deleted && isCanvasDocumentKey(key)) canvasItemCache().removeCanvas(key);
        return deleted;
    } catch (...) {
        return false;
    }
}

CanvasMemory::CompareSetResult CanvasMemory::deleteIfCacheGenerationMatches(
        const std::string& key, const std::string& generation) {
    auto document_lock = lockCanvasDocument(key);
    if (isCanvasDocumentKey(key) && !flushCanvasItemCacheLocked(key)) return CompareSetResult::Error;
    static const std::string script = R"LUA(
local value = redis.call('JSON.GET', KEYS[1], '$["_cache_generation"]')
if not value then return 'CONFLICT' end
local decoded = cjson.decode(value)
if decoded[1] ~= ARGV[1] then return 'CONFLICT' end
redis.call('DEL', KEYS[1])
return 'APPLIED'
)LUA";
    if (!sendCommand({"EVAL", script, "1", key, generation})) {
        if (isCanvasDocumentKey(key)) canvasItemCache().removeCanvas(key);
        return CompareSetResult::Error;
    }
    const auto response = readResponse();
    if (response == "APPLIED") {
        if (isCanvasDocumentKey(key)) canvasItemCache().removeCanvas(key);
        return CompareSetResult::Applied;
    }
    if (response == "CONFLICT") return CompareSetResult::Conflict;
    if (isCanvasDocumentKey(key)) canvasItemCache().removeCanvas(key);
    return CompareSetResult::Error;
}

bool CanvasMemory::deletePattern(const std::string& pattern) {
    // Pattern deletion can include any canvas document, so block cache-backed
    // reads while discovering and deleting the matching Redis keys.
    std::vector<std::unique_lock<std::mutex>> document_locks;
    document_locks.reserve(canvasDocumentMutexes().size());
    for (auto& mutex : canvasDocumentMutexes()) document_locks.emplace_back(mutex);

    if (!sendCommand({"KEYS", pattern})) return false;
    std::string keys_str = readResponse();
    if (isRedisErrorResponse(keys_str)) return false;
    if (keys_str.empty()) return true;

    std::istringstream iss(keys_str);
    std::string key;
    std::vector<std::string> del_args = {"DEL"};
    std::vector<std::string> deleted_canvas_keys;
    while (iss >> key) {
        if (isCanvasDocumentKey(key)) deleted_canvas_keys.push_back(key);
        del_args.push_back(key);
    }
    if (del_args.size() > 1) {
        if (!sendCommand(del_args)) return false;
        const std::string response = readResponse();
        try {
            const bool deleted = std::stoi(response) >= 0;
            if (deleted) {
                for (const auto& canvas_key : deleted_canvas_keys) {
                    canvasItemCache().removeCanvas(canvas_key);
                }
            }
            return deleted;
        } catch (...) { return false; }
    }
    return true;
}

int CanvasMemory::getKeyCount(const std::string& pattern) {
    if (!sendCommand({"KEYS", pattern})) return 0;
    std::string keys_str = readResponse();
    if (keys_str.empty()) return 0;
    std::istringstream iss(keys_str);
    std::string key;
    int count = 0;
    while (iss >> key) count++;
    return count;
}

bool CanvasMemory::updateJson(const std::string& key, const std::function<void(nlohmann::json&)>& modifier) {
    auto current = get(key);
    nlohmann::json root;
    if (current && !current->empty()) {
        try {
            root = nlohmann::json::parse(*current);
        } catch (...) {
            root = nlohmann::json::object();
        }
    } else {
        root = nlohmann::json::object();
    }

    modifier(root);
    return set(key, root.dump());
}

namespace {
std::string canvasKey(int id) { return "canvas:" + std::to_string(id); }
std::string fieldPath(const std::string& field) {
    return "$[" + nlohmann::json(field).dump() + "]";
}
}

std::optional<std::string> CanvasMemory::loadCanvas(int id) { return get(canvasKey(id)); }
bool CanvasMemory::storeCanvas(int id, const nlohmann::json& document) {
    return set(canvasKey(id), document.dump());
}
bool CanvasMemory::flushCanvas(int id) { return flushCanvasItemCache(canvasKey(id)); }
bool CanvasMemory::clearCanvas(int id) {
    return deletePattern(canvasKey(id) + ":*") && del(canvasKey(id));
}
CanvasMemory::CompareSetResult CanvasMemory::removeCanvasGeneration(int id, const std::string& generation) {
    return deleteIfCacheGenerationMatches(canvasKey(id), generation);
}
std::optional<std::string> CanvasMemory::readItem(int id, const std::string& item_id, const std::string& field) {
    auto path = canvasItemJsonPath(item_id);
    if (!field.empty()) path += fieldPath(field).substr(1);
    return getJsonPath(canvasKey(id), path);
}
bool CanvasMemory::storeItem(int id, const std::string& item_id, const nlohmann::json& item) {
    return setJsonPath(canvasKey(id), canvasItemJsonPath(item_id), item);
}
bool CanvasMemory::removeItem(int id, const std::string& item_id) {
    return deleteJsonPath(canvasKey(id), canvasItemJsonPath(item_id));
}
bool CanvasMemory::replaceItems(int id, const nlohmann::json& items) {
    return setJsonPath(canvasKey(id), "$.items", items);
}
bool CanvasMemory::appendMessage(int id, const std::string& room, std::uint64_t sequence, const nlohmann::json& message) {
    return appendChatMessage(canvasKey(id), room, sequence, message);
}
std::optional<std::string> CanvasMemory::readChatPage(int id, const std::string& room,
        const std::optional<std::uint64_t>& from, const std::optional<std::uint64_t>& to, std::uint64_t limit) {
    return getChatHistoryPage(canvasKey(id), room, from, to, limit);
}
CanvasMemory::CompareSetResult CanvasMemory::compareAndSetFields(int id, long long revision,
        const std::vector<std::pair<std::string, nlohmann::json>>& fields) {
    std::vector<std::pair<std::string, nlohmann::json>> paths;
    paths.reserve(fields.size());
    for (const auto& [name, value] : fields) paths.emplace_back(fieldPath(name), value);
    return compareAndSetJsonPaths(canvasKey(id), revision, paths);
}
