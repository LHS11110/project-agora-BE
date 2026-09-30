#include "RedisClient.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include "Environment.hpp"
#include <Poco/LRUCache.h>
#include <iostream>
#include <sstream>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#include <netdb.h>
#include <thread>
#include <chrono>
#include <climits>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <openssl/err.h>
#include <openssl/x509v3.h>

namespace {
constexpr std::size_t kCanvasItemCacheDefaultCapacity = 64;
constexpr std::size_t kCanvasItemCacheMaximumCapacity = 4096;
constexpr std::size_t kCanvasCacheDefaultMaximumCanvases = 256;
constexpr std::size_t kCanvasCacheMaximumCanvases = 4096;
constexpr std::size_t kMaximumCanvasItemCacheBytes = 256 * 1024;
constexpr std::size_t kMaximumRedisBulkReplyBytes = 256 * 1024 * 1024;
constexpr std::size_t kMaximumRedisArrayElements = 1'000'000;
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

std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    return environmentValue(name);
}

bool envEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return false;
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized == "true" || normalized == "1" || normalized == "yes";
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

bool parseRespLength(const std::string& text, long long& value) {
    if (text.empty()) return false;
    try {
        std::size_t parsed_length = 0;
        value = std::stoll(text, &parsed_length);
        return parsed_length == text.size();
    } catch (...) {
        return false;
    }
}

std::vector<std::pair<std::string, int>> parseSentinelSeeds(const char* configured) {
    std::vector<std::pair<std::string, int>> seeds;
    if (!configured || !*configured) return seeds;

    std::istringstream input(configured);
    std::string entry;
    while (std::getline(input, entry, ',')) {
        const auto first = entry.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const auto last = entry.find_last_not_of(" \t\r\n");
        entry = entry.substr(first, last - first + 1);

        std::string host;
        std::string port_text;
        if (!entry.empty() && entry.front() == '[') {
            const auto close = entry.find(']');
            if (close == std::string::npos || close + 1 >= entry.size() || entry[close + 1] != ':') continue;
            host = entry.substr(1, close - 1);
            port_text = entry.substr(close + 2);
        } else {
            const auto separator = entry.rfind(':');
            if (separator == std::string::npos) continue;
            host = entry.substr(0, separator);
            port_text = entry.substr(separator + 1);
        }

        try {
            const int port = std::stoi(port_text);
            if (!host.empty() && port > 0 && port <= 65535) seeds.emplace_back(host, port);
        } catch (...) {
            continue;
        }
    }
    return seeds;
}
}

RedisClient::RedisClient(const std::string& /*logical_host*/, int /*logical_port*/,
                         const std::string& user, const std::string& password)
    : user_(envOr("REDIS_USER", user)),
      password_(envOr("REDIS_USER_PASSWORD", password)),
      sentinel_master_name_(envOr("REDIS_SENTINEL_MASTER_NAME", "agora-master")),
      sentinel_user_(envOr("REDIS_SENTINEL_USER", "")),
      sentinel_password_(envOr("REDIS_SENTINEL_PASSWORD", "")),
      sentinel_seeds_(parseSentinelSeeds(std::getenv("REDIS_SENTINELS"))),
      tls_ca_cert_(envOr("REDIS_TLS_CA_CERT", "")),
      tls_enabled_(envEnabled("REDIS_TLS_ENABLED")), tls_config_valid_(!tls_enabled_),
      socket_fd_(-1), ssl_context_(nullptr), ssl_(nullptr) {
    if (!tls_enabled_) return;
    if (tls_ca_cert_.empty()) {
        std::cerr << "[RedisClient] REDIS_TLS_ENABLED requires REDIS_TLS_CA_CERT\n";
        return;
    }

    ssl_context_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_context_) {
        std::cerr << "[RedisClient] Could not create a Redis TLS client context\n";
        return;
    }
    SSL_CTX_set_min_proto_version(ssl_context_, TLS1_2_VERSION);
    SSL_CTX_set_verify(ssl_context_, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_load_verify_locations(ssl_context_, tls_ca_cert_.c_str(), nullptr) != 1) {
        std::cerr << "[RedisClient] Could not load REDIS_TLS_CA_CERT\n";
        SSL_CTX_free(ssl_context_);
        ssl_context_ = nullptr;
        return;
    }
    tls_config_valid_ = true;
}

RedisClient::~RedisClient() {
    disconnect();
    if (ssl_context_) SSL_CTX_free(ssl_context_);
}

bool RedisClient::connect() {
    if (socket_fd_ >= 0) {
        return true;
    }

    if (password_.empty()) {
        std::cerr << "[RedisClient] REDIS_USER_PASSWORD is not configured\n";
        return false;
    }
    if (tls_enabled_ && !tls_config_valid_) {
        std::cerr << "[RedisClient] Redis TLS is enabled but its CA configuration is invalid\n";
        return false;
    }
    if (sentinel_seeds_.empty()) {
        std::cerr << "[RedisClient] REDIS_SENTINELS is required; direct Redis connections are disabled\n";
        return false;
    }
    if (sentinel_user_.empty() || sentinel_password_.empty()) {
        std::cerr << "[RedisClient] REDIS_SENTINEL_USER and REDIS_SENTINEL_PASSWORD are required\n";
        return false;
    }

    const auto authenticate = [this]() {
        if (user_.empty()) {
            if (!sendCommand({"AUTH", password_})) return false;
        } else if (!sendCommand({"AUTH", user_, password_})) {
            return false;
        }
        const std::string response = readResponse();
        if (response != "OK") {
            std::cerr << "[RedisClient] Redis authentication failed\n";
            disconnect();
            return false;
        }
        return true;
    };

    for (int attempt = 0; attempt < 3; ++attempt) {
        for (const auto& seed : sentinel_seeds_) {
            disconnect();
            if (!connectTo(seed.first, seed.second, 700)) continue;
            const auto auth = std::vector<std::string>{"AUTH", sentinel_user_, sentinel_password_};
            if (!sendCommand(auth) || readResponse() != "OK") {
                disconnect();
                continue;
            }
            if (!sendCommand({"SENTINEL", "get-master-addr-by-name", sentinel_master_name_})) {
                disconnect();
                continue;
            }
            const std::string master_reply = readResponse();
            disconnect();

            std::istringstream master_parts(master_reply);
            std::string master_host;
            int master_port = 0;
            if (!(master_parts >> master_host >> master_port) || master_host.empty() || master_port <= 0 || master_port > 65535) {
                continue;
            }
            if (!connectTo(master_host, master_port, 1200) || !authenticate()) continue;

            if (!sendCommand({"ROLE"})) continue;
            std::istringstream role_parts(readResponse());
            std::string role;
            role_parts >> role;
            if (role == "master") {
                const std::string endpoint = master_host + ":" + std::to_string(master_port);
                ElasticsearchBulkLogBuffer::instance().reportAvailability(
                        "redis-sentinel", true, {{"endpoint", endpoint}});
                ElasticsearchBulkLogBuffer::instance().reportPrimaryChange("redis-sentinel", endpoint);
                return true;
            }

            // Sentinel can briefly return its previous view during promotion.
            // Verify the role on the data node before accepting the connection.
            disconnect();
        }
        if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
    }
    ElasticsearchBulkLogBuffer::instance().reportAvailability(
            "redis-sentinel", false, {{"seed_count", sentinel_seeds_.size()}});
    std::cerr << "[RedisClient] Could not discover a writable Redis primary from Sentinel\n";
    disconnect();
    return false;
}

bool RedisClient::connectTo(const std::string& host, int port, int timeout_ms) {
    if (socket_fd_ >= 0) disconnect();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) return false;

    bool connected = false;
    for (addrinfo* address = addresses; address && !connected; address = address->ai_next) {
        int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv{};
        tv.tv_sec = 2;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

        const int original_flags = fcntl(fd, F_GETFL, 0);
        if (original_flags >= 0) fcntl(fd, F_SETFL, original_flags | O_NONBLOCK);
        int result = ::connect(fd, address->ai_addr, address->ai_addrlen);
        if (result < 0 && errno == EINPROGRESS) {
            pollfd pfd{fd, POLLOUT, 0};
            result = poll(&pfd, 1, timeout_ms);
            if (result > 0) {
                int socket_error = 0;
                socklen_t error_length = sizeof(socket_error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) == 0 && socket_error == 0) {
                    result = 0;
                } else {
                    result = -1;
                }
            } else {
                result = -1;
            }
        }
        if (original_flags >= 0) fcntl(fd, F_SETFL, original_flags);
        if (result == 0) {
            SSL* candidate_ssl = nullptr;
            if (tls_enabled_) {
                candidate_ssl = SSL_new(ssl_context_);
                if (!candidate_ssl || SSL_set_fd(candidate_ssl, fd) != 1) {
                    if (candidate_ssl) SSL_free(candidate_ssl);
                    close(fd);
                    continue;
                }

                in_addr ipv4{};
                in6_addr ipv6{};
                X509_VERIFY_PARAM* verify = SSL_get0_param(candidate_ssl);
                const bool is_ip = inet_pton(AF_INET, host.c_str(), &ipv4) == 1
                        || inet_pton(AF_INET6, host.c_str(), &ipv6) == 1;
                const bool identity_set = is_ip
                        ? X509_VERIFY_PARAM_set1_ip_asc(verify, host.c_str()) == 1
                        : X509_VERIFY_PARAM_set1_host(verify, host.c_str(), 0) == 1;
                if ((!is_ip && SSL_set_tlsext_host_name(candidate_ssl, host.c_str()) != 1)
                        || !identity_set || SSL_connect(candidate_ssl) != 1
                        || SSL_get_verify_result(candidate_ssl) != X509_V_OK) {
                    SSL_free(candidate_ssl);
                    close(fd);
                    ERR_clear_error();
                    continue;
                }
            }
            socket_fd_ = fd;
            ssl_ = candidate_ssl;
            connected = true;
        } else {
            close(fd);
        }
    }
    freeaddrinfo(addresses);
    return connected;
}

void RedisClient::disconnect() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

ssize_t RedisClient::readTransport(void* buffer, std::size_t size) {
    if (ssl_) return SSL_read(ssl_, buffer, static_cast<int>(std::min(size, static_cast<std::size_t>(INT_MAX))));
    return ::read(socket_fd_, buffer, size);
}

ssize_t RedisClient::writeTransport(const void* buffer, std::size_t size) {
    if (ssl_) return SSL_write(ssl_, buffer, static_cast<int>(std::min(size, static_cast<std::size_t>(INT_MAX))));
    return ::write(socket_fd_, buffer, size);
}

bool RedisClient::sendCommand(const std::vector<std::string>& args) {
    if (socket_fd_ < 0 && !connect()) {
        return false;
    }

    std::string msg;
    msg.push_back('*');
    msg.append(std::to_string(args.size()));
    msg.append("\r\n");
    for (const auto& arg : args) {
        msg.push_back('$');
        msg.append(std::to_string(arg.size()));
        msg.append("\r\n");
        msg.append(arg);
        msg.append("\r\n");
    }

    std::size_t total = 0;
    while (total < msg.size()) {
        ssize_t sent = writeTransport(msg.data() + total, msg.size() - total);
        if (sent <= 0) {
            disconnect();
            return false;
        }
        total += static_cast<std::size_t>(sent);
    }
    return true;
}

std::string RedisClient::readLine() {
    std::string line;
    char c;
    while (socket_fd_ >= 0) {
        const ssize_t received = readTransport(&c, 1);
        if (received != 1) {
            disconnect();
            return "";
        }
        if (c == '\r') {
            char next_c;
            if (readTransport(&next_c, 1) == 1 && next_c == '\n') {
                break;
            }
            disconnect();
            return "";
        } else {
            // RESP line headers and simple strings should remain small. Reject
            // malformed peers instead of growing this buffer without a bound.
            if (line.size() >= 64 * 1024) {
                disconnect();
                return "";
            }
            line += c;
        }
    }
    return line;
}

std::string RedisClient::readResponse() {
    if (socket_fd_ < 0) return "";
    std::string prefix = readLine();
    if (prefix.empty()) return "";

    char type = prefix[0];
    if (type == '-') {
        const std::string error = prefix.substr(1);
        // A promoted former primary returns READONLY while its socket can still
        // look healthy. Drop it so the next request re-queries Sentinel. The
        // current command is deliberately not replayed because its result may
        // be uncertain after failover.
        if (error.rfind("READONLY", 0) == 0 || error.rfind("MASTERDOWN", 0) == 0) {
            disconnect();
            ElasticsearchBulkLogBuffer::instance().reportAvailability(
                    sentinel_seeds_.empty() ? "redis" : "redis-sentinel", false,
                    {{"error", error.substr(0, 64)}});
        }
        return std::string(1, kRedisErrorMarker) + error;
    } else if (type == '+' || type == ':') {
        return prefix.substr(1);
    } else if (type == '$') {
        long long parsed_length = 0;
        if (!parseRespLength(prefix.substr(1), parsed_length)
                || parsed_length < -1
                || parsed_length > static_cast<long long>(kMaximumRedisBulkReplyBytes)) {
            disconnect();
            return "";
        }
        if (parsed_length == -1) return "";
        const auto length = static_cast<std::size_t>(parsed_length);
        std::string result(length, '\0');
        ssize_t total = 0;
        while (static_cast<std::size_t>(total) < length) {
            ssize_t r = readTransport(result.data() + total, length - static_cast<std::size_t>(total));
            if (r <= 0) break;
            total += r;
        }
        // consume \r\n
        char crlf[2];
        ssize_t crlf_total = 0;
        while (crlf_total < 2) {
            ssize_t r = readTransport(crlf + crlf_total, 2 - crlf_total);
            if (r <= 0) break;
            crlf_total += r;
        }
        if (static_cast<std::size_t>(total) != length || crlf_total != 2
                || crlf[0] != '\r' || crlf[1] != '\n') {
            disconnect();
            return "";
        }
        return result;
    } else if (type == '*') {
        long long parsed_count = 0;
        if (!parseRespLength(prefix.substr(1), parsed_count)
                || parsed_count < -1
                || parsed_count > static_cast<long long>(kMaximumRedisArrayElements)) {
            disconnect();
            return "";
        }
        if (parsed_count == -1) return "";
        std::string result;
        for (long long i = 0; i < parsed_count; ++i) {
            std::string item = readResponse();
            if (i > 0) result += " ";
            result += item;
        }
        return result;
    }
    return prefix;
}

bool RedisClient::ping() {
    if (!sendCommand({"PING"})) return false;
    std::string res = readResponse();
    return res == "PONG";
}

bool RedisClient::set(const std::string& key, const std::string& value) {
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

std::optional<std::string> RedisClient::get(const std::string& key) {
    auto document_lock = lockCanvasDocument(key);
    auto document = readFromRedis(key);
    if (!document || !isCanvasDocumentKey(key)) return document;
    return overlayDirtyCanvasItems(key, *document);
}

bool RedisClient::flushCanvasItemCache(const std::string& key) {
    auto document_lock = lockCanvasDocument(key);
    if (!isCanvasDocumentKey(key)) return false;
    return flushCanvasItemCacheLocked(key);
}

bool RedisClient::flushCanvasItemCacheLocked(const std::string& key) {
    const CanvasItemWriter writer = [this](const std::string& canvas_key,
                                           const std::string& item_id,
                                           const std::string& item_json) {
        return writeBackCanvasItem(canvas_key, item_id, item_json);
    };
    return canvasItemCache().flush(key, writer);
}

std::optional<std::string> RedisClient::readFromRedis(const std::string& key) {
    if (!sendCommand({"JSON.GET", key})) return std::nullopt;
    std::string res = readResponse();
    if (isWrongTypeResponse(res)) {
        if (!sendCommand({"GET", key})) return std::nullopt;
        res = readResponse();
    }
    if (res.empty() || isRedisErrorResponse(res)) return std::nullopt;
    return res;
}

std::optional<std::string> RedisClient::readJsonPathFromRedis(
        const std::string& key, const std::string& path) {
    if (!sendCommand({"JSON.GET", key, path})) return std::nullopt;
    const std::string response = readResponse();
    if (response.empty() || isRedisErrorResponse(response)) return std::nullopt;
    return response;
}

bool RedisClient::writeBackCanvasItem(const std::string& key, const std::string& item_id,
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

std::optional<std::string> RedisClient::getJsonPath(const std::string& key, const std::string& path) {
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

bool RedisClient::setJsonPath(const std::string& key, const std::string& path, const nlohmann::json& value) {
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

bool RedisClient::appendChatMessage(const std::string& key, const std::string& item_id,
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

std::optional<std::string> RedisClient::getChatHistoryPage(
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

RedisClient::CompareSetResult RedisClient::compareAndSetJsonPaths(
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

bool RedisClient::deleteJsonPath(const std::string& key, const std::string& path) {
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

bool RedisClient::del(const std::string& key) {
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

RedisClient::CompareSetResult RedisClient::deleteIfCacheGenerationMatches(
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

bool RedisClient::deletePattern(const std::string& pattern) {
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

int RedisClient::getKeyCount(const std::string& pattern) {
    if (!sendCommand({"KEYS", pattern})) return 0;
    std::string keys_str = readResponse();
    if (keys_str.empty()) return 0;
    std::istringstream iss(keys_str);
    std::string key;
    int count = 0;
    while (iss >> key) count++;
    return count;
}

bool RedisClient::updateJson(const std::string& key, const std::function<void(nlohmann::json&)>& modifier) {
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
