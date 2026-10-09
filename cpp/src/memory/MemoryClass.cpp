#include "memory/MemoryClass.hpp"
#include "memory/RedisMemory.hpp"
#include "memory/LruMemory.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
namespace {
struct CacheLookup {
    std::optional<std::string> value;
    bool admit_on_miss{false};
};

using RegionWriter = std::function<bool(const std::string&, const std::string&, const std::string&)>;



class RegionLru {
public:
    RegionLru(std::size_t capacity, std::size_t maximum_bytes)
        : capacity_(capacity), maximum_bytes_(maximum_bytes), entries_(capacity), admission_(capacity) {}

    CacheLookup lookup(const std::string& region) {
        if (capacity_ == 0) return {};
        if (const auto cached = entries_.get(region)) {
            touch(region);
            return {*cached, false};
        }
        if (const auto state = admission_.get(region)) {
            if (*state == kUncacheable) return {};
            admission_.remove(region);
            return {std::nullopt, true};
        }
        admission_.add(region, kFirstAccess);
        return {};
    }

    std::optional<std::string> get(const std::string& region) {
        if (capacity_ == 0) return std::nullopt;
        const auto cached = entries_.get(region);
        if (!cached) return std::nullopt;
        touch(region);
        return *cached;
    }

    bool add(const std::string& document_key, const std::string& region,
             const std::string& value_json, bool dirty, const RegionWriter& writer) {
        if (capacity_ == 0) return true;
        const auto current = entries_.get(region);
        const bool already_cached = static_cast<bool>(current);
        if (already_cached) touch(region);
        if (value_json.size() > maximum_bytes_) {
            if (already_cached && dirty && !writer(document_key, region, value_json)) return false;
            if (already_cached) erase(region);
            admission_.add(region, kUncacheable);
            return true;
        }
        if (!already_cached && lru_.size() >= capacity_) {
            const std::string victim = lru_.back();
            if (dirty_regions_.count(victim)) {
                const auto victim_json = entries_.get(victim);
                if (!victim_json || !writer(document_key, victim, *victim_json)) return false;
            }
            erase(victim);
        }
        admission_.remove(region);
        entries_.add(region, value_json);
        touch(region);
        if (dirty) dirty_regions_.insert(region);
        else dirty_regions_.erase(region);
        return true;
    }

    void remove(const std::string& region) {
        erase(region);
        admission_.remove(region);
    }

    bool update(const std::string& document_key, const std::string& region,
                const std::string& value_json, const RegionWriter& writer) {
        if (!entries_.get(region)) return false;
        return add(document_key, region, value_json, true, writer);
    }

    bool markClean(const std::string& region) {
        if (!entries_.get(region)) return false;
        touch(region);
        dirty_regions_.erase(region);
        return true;
    }

    std::vector<std::pair<std::string, std::string>> dirtyRegions() {
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(dirty_regions_.size());
        for (const auto& region : dirty_regions_) {
            const auto item = entries_.get(region);
            if (item) {
                touch(region);
                result.emplace_back(region, *item);
            }
        }
        return result;
    }

    bool flush(const std::string& document_key, const RegionWriter& writer) {
        const auto pending = dirtyRegions();
        for (const auto& [region, value_json] : pending) {
            if (!writer(document_key, region, value_json)) return false;
            dirty_regions_.erase(region);
        }
        return true;
    }

private:
    static constexpr int kFirstAccess = 1;
    static constexpr int kUncacheable = 2;

    void touch(const std::string& region) {
        const auto found = lru_positions_.find(region);
        if (found != lru_positions_.end()) lru_.erase(found->second);
        lru_.push_front(region);
        lru_positions_[region] = lru_.begin();
    }

    void erase(const std::string& region) {
        entries_.remove(region);
        dirty_regions_.erase(region);
        const auto found = lru_positions_.find(region);
        if (found != lru_positions_.end()) {
            lru_.erase(found->second);
            lru_positions_.erase(found);
        }
    }

    std::size_t capacity_;
    std::size_t maximum_bytes_;
    LruMemory<std::string> entries_;
    LruMemory<int> admission_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_positions_;
    std::unordered_set<std::string> dirty_regions_;
};

class CacheManager {
public:
    CacheManager(std::size_t maximum_documents, std::size_t regions_per_document,
                 std::size_t maximum_bytes, std::array<std::mutex, 64>& stripes)
        : maximum_bytes_(maximum_bytes), stripes_(stripes),
          maximum_documents_(maximum_documents), maximum_regions_(regions_per_document) {}

    CacheLookup lookup(const std::string& document_key, const std::string& region,
                                 const RegionWriter& writer) {
        if (maximum_regions_ == 0) return {};
        const auto cache = cacheForLookup(document_key, writer);
        return cache ? cache->lookup(region) : CacheLookup{};
    }

    std::optional<std::string> get(const std::string& document_key, const std::string& region) {

        const auto cache = find(document_key);
        return cache ? cache->get(region) : std::nullopt;
    }

    bool admit(const std::string& document_key, const std::string& region,
               const std::string& value_json, const RegionWriter& writer) {
        if (maximum_regions_ == 0) return false;
        const auto cache = cacheForLookup(document_key, writer);
        return cache && cache->add(document_key, region, value_json, false, writer);
    }

    bool update(const std::string& document_key, const std::string& region,
                const std::string& value_json, const RegionWriter& writer) {
        const auto cache = find(document_key);
        return cache && cache->update(document_key, region, value_json, writer);
    }

    bool markClean(const std::string& document_key, const std::string& region) {
        const auto cache = find(document_key);
        return cache && cache->markClean(region);
    }

    std::vector<std::pair<std::string, std::string>> dirtyRegions(const std::string& document_key) {
        const auto cache = find(document_key);
        return cache ? cache->dirtyRegions() : std::vector<std::pair<std::string, std::string>>{};
    }

    bool flush(const std::string& document_key, const RegionWriter& writer) {
        const auto cache = find(document_key);
        return !cache || cache->flush(document_key, writer);
    }

    void removeRegion(const std::string& document_key, const std::string& region) {

        const auto cache = find(document_key);
        if (cache) cache->remove(region);
    }

    void removeDocument(const std::string& document_key) {

        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = caches_.find(document_key);
        if (found == caches_.end()) return;
        caches_.erase(found);
        const auto position = lru_positions_.find(document_key);
        if (position != lru_positions_.end()) {
            lru_.erase(position->second);
            lru_positions_.erase(position);
        }
    }

private:
    std::shared_ptr<RegionLru> find(const std::string& document_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = caches_.find(document_key);
        if (found == caches_.end()) return nullptr;
        touch(document_key);
        return found->second;
    }

    void touch(const std::string& document_key) {
        const auto found = lru_positions_.find(document_key);
        if (found != lru_positions_.end()) lru_.erase(found->second);
        lru_.push_front(document_key);
        lru_positions_[document_key] = lru_.begin();
    }

    std::shared_ptr<RegionLru> cacheForLookup(const std::string& document_key,
                                                  const RegionWriter& writer) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = caches_.find(document_key);
        if (found != caches_.end()) {
            touch(document_key);
            return found->second;
        }
        if (maximum_documents_ == 0) return nullptr;
        if (caches_.size() >= maximum_documents_) {
            bool evicted = false;
            for (auto candidate = lru_.rbegin(); candidate != lru_.rend(); ++candidate) {
                const auto victim = caches_.find(*candidate);
                if (victim == caches_.end()) continue;
                // Callers hold their own document stripe; never block on a
                // different stripe while holding the cache-manager mutex.
                std::unique_lock<std::mutex> victim_document_lock(
                    stripes_[std::hash<std::string>{}(*candidate) % stripes_.size()], std::try_to_lock);
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
        auto created = std::make_shared<RegionLru>(maximum_regions_, maximum_bytes_);
        caches_[document_key] = created;
        touch(document_key);
        return created;
    }

    std::size_t maximum_bytes_;
    std::array<std::mutex, 64>& stripes_;
    std::size_t maximum_documents_;
    std::size_t maximum_regions_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<RegionLru>> caches_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_positions_;
};

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
            if (path.substr(begin, position - begin).find_first_of("*:?@()") != std::string::npos) return false;
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

std::string canonical(const std::vector<JsonPathSegment>& segments) {
    std::string path = "$";
    for (const auto& segment : segments)
        path += segment.is_array_index ? "[" + std::to_string(segment.index) + "]"
                                      : "[" + nlohmann::json(segment.key).dump() + "]";
    return path;
}
std::string result(const nlohmann::json& value, const std::vector<JsonPathSegment>& path) {
    const nlohmann::json* found = nullptr;
    return findJsonPathValue(value, path, found) ? nlohmann::json::array({*found}).dump() : "[]";
}
bool error(const std::optional<std::string>& reply) {
    return !reply || reply->empty() || reply->front() == '\x01';
}
}
struct MemoryClass::State {
    explicit State(Options settings) : options(std::move(settings)),
        cache(options.maximum_documents, options.entries_per_document,
              options.maximum_entry_bytes, stripes) {}
    Options options;
    std::array<std::mutex, 64> stripes;
    CacheManager cache;
};
struct MemoryClass::Impl {
    Impl(std::unique_ptr<RedisCommandExecutor> transport, std::shared_ptr<State> shared)
        : backend(std::move(transport)), state(shared ? std::move(shared) : createState(Options{})) {}
    RedisMemory backend;
    std::shared_ptr<State> state;
    std::mutex transport_mutex;
    auto lock(const std::string& key) { return std::unique_lock<std::mutex>(state->stripes[std::hash<std::string>{}(key) % state->stripes.size()]); }
    std::optional<std::string> execute(const std::vector<std::string>& command) {
        std::lock_guard<std::mutex> guard(transport_mutex);
        return backend.execute(command);
    }
    std::vector<std::string> remote(const nlohmann::json& value) {
        return state->options.remote_fields ? state->options.remote_fields(value) : std::vector<std::string>{};
    }
    nlohmann::json project(nlohmann::json value) {
        if (value.is_object()) for (const auto& field : remote(value)) value.erase(field);
        return value;
    }
    bool excluded(const nlohmann::json& value, const std::vector<JsonPathSegment>& relative) {
        auto fields = remote(value);
        if (fields.empty()) return false;
        return relative.empty() || (!relative.front().is_array_index &&
            std::find(fields.begin(), fields.end(), relative.front().key) != fields.end());
    }
    std::optional<nlohmann::json> raw(const std::string& key, const std::string& path) {
        auto reply = execute({"JSON.GET", key, path});
        if (error(reply)) return {};
        try { auto matches = nlohmann::json::parse(*reply);
            if (matches.is_array() && !matches.empty()) return matches.front();
        } catch (...) {}
        return {};
    }
    nlohmann::json merge(nlohmann::json cached, const nlohmann::json& original) {
        if (cached.is_object() && original.is_object())
            for (const auto& field : remote(cached))
                if (!cached.contains(field) && original.contains(field)) cached[field] = original[field];
        return cached;
    }
    bool writeBack(const std::string& key, const std::string& path, const std::string& value) {
        auto parsed = nlohmann::json::parse(value);
        if (!remote(parsed).empty()) {
            auto current = raw(key, path);
            if (!current) return false;
            parsed = merge(std::move(parsed), *current);
        }
        return execute({"JSON.SET", key, path, parsed.dump()}) == "OK";
    }
    RegionWriter writer() { return [this](const auto& key, const auto& path, const auto& value) { return writeBack(key, path, value); }; }
    bool region(const std::string& path, std::string& parent, std::vector<JsonPathSegment>& relative) {
        std::vector<JsonPathSegment> segments;
        if (!parseJsonPath(path, segments) || segments.size() < state->options.region_depth) return false;
        auto end = segments.begin() + state->options.region_depth;
        parent = canonical({segments.begin(), end});
        relative.assign(end, segments.end());
        return true;
    }
    void invalidate(const std::string& key, const std::string& path) {
        std::string parent; std::vector<JsonPathSegment> relative;
        if (!region(path, parent, relative)) state->cache.removeDocument(key);
        else state->cache.removeRegion(key, parent);
    }
    std::optional<std::string> document(const std::string& key) {
        auto reply = execute({"JSON.GET", key});
        if (reply && reply->find("WRONGTYPE") != std::string::npos) reply = execute({"GET", key});
        if (error(reply)) return {};
        try {
            auto value = nlohmann::json::parse(*reply);
            for (const auto& [path, serialized] : state->cache.dirtyRegions(key)) {
                std::vector<JsonPathSegment> segments; parseJsonPath(path, segments);
                auto updated = nlohmann::json::parse(serialized);
                const nlohmann::json* original = nullptr;
                if (findJsonPathValue(value, segments, original)) updated = merge(std::move(updated), *original);
                if (!setJsonPathValue(value, segments, updated)) return {}; // Never hide dirty data.
            }
            return value.dump();
        } catch (...) { return {}; }
    }
};
std::shared_ptr<MemoryClass::State> MemoryClass::createState(Options options) { return std::make_shared<State>(std::move(options)); }
MemoryClass::MemoryClass(std::unique_ptr<RedisCommandExecutor> transport, std::shared_ptr<State> state)
    : impl_(std::make_unique<Impl>(std::move(transport), std::move(state))) {}
MemoryClass::MemoryClass(const std::string& host, int port, const std::string& user, const std::string& password, std::shared_ptr<State> state)
    : MemoryClass(std::make_unique<RedisMemory>(host, port, user, password), std::move(state)) {}
MemoryClass::~MemoryClass() = default;
bool MemoryClass::connect() { std::lock_guard<std::mutex> guard(impl_->transport_mutex); return impl_->backend.connect(); }
void MemoryClass::disconnect() { std::lock_guard<std::mutex> guard(impl_->transport_mutex); impl_->backend.disconnect(); }
bool MemoryClass::ping() { return impl_->execute({"PING"}) == "PONG"; }
std::optional<std::string> MemoryClass::read(const std::string& key) {
    if (impl_->state->options.region_depth == 0) {
        auto value = read(key, "$");
        if (!value) return {};
        try { auto matches = nlohmann::json::parse(*value); if (!matches.empty()) return matches.front().dump(); } catch (...) {}
        return {};
    }
    auto guard = impl_->lock(key);
    return impl_->document(key);
}
std::optional<std::string> MemoryClass::read(const std::string& key, const std::string& path) {
    auto guard = impl_->lock(key);
    std::string parent; std::vector<JsonPathSegment> relative;
    if (!impl_->region(path, parent, relative)) {
        std::vector<JsonPathSegment> segments;
        if (parseJsonPath(path, segments)) {
            auto doc = impl_->document(key);
            if (!doc) return {};
            return result(nlohmann::json::parse(*doc), segments);
        }
        if (!impl_->state->cache.flush(key, impl_->writer())) return {};
        auto reply = impl_->execute({"JSON.GET", key, path});
        return error(reply) ? std::nullopt : reply;
    }
    const auto lookup = impl_->state->cache.lookup(key, parent, impl_->writer());
    if (lookup.value) {
        auto value = nlohmann::json::parse(*lookup.value);
        if (!impl_->excluded(value, relative)) return result(value, relative);
        auto current = impl_->raw(key, parent);
        if (!current) return {};
        return result(impl_->merge(std::move(value), *current), relative);
    }
    if (lookup.admit_on_miss) {
        auto value = impl_->raw(key, parent);
        if (!value) { auto reply = impl_->execute({"JSON.GET", key, path}); return error(reply) ? std::nullopt : reply; }
        impl_->state->cache.admit(key, parent, impl_->project(*value).dump(), impl_->writer());
        return result(*value, relative);
    }
    auto reply = impl_->execute({"JSON.GET", key, path});
    return error(reply) ? std::nullopt : reply;
}
bool MemoryClass::write(const std::string& key, const nlohmann::json& value, const std::string& path) {
    auto guard = impl_->lock(key);
    std::string parent; std::vector<JsonPathSegment> relative;
    if (impl_->region(path, parent, relative)) {
        auto cached = impl_->state->cache.get(key, parent);
        if (cached) {
            auto updated = nlohmann::json::parse(*cached);
            if (!impl_->excluded(updated, relative) || relative.empty()) {
                // Explicit remote-field replacements must be written through;
                // a metadata-only replacement preserves remote fields.
                bool replaces_remote = relative.empty() && value.is_object();
                if (replaces_remote) {
                    replaces_remote = false;
                    for (const auto& field : impl_->remote(updated)) replaces_remote |= value.contains(field);
                }
                if (!replaces_remote) {
                    if (!setJsonPathValue(updated, relative, value)) return false;
                    return impl_->state->cache.update(key, parent, impl_->project(std::move(updated)).dump(), impl_->writer());
                }
            }
            if (!impl_->state->cache.flush(key, impl_->writer())) return false;
        }
    } else if (!impl_->state->cache.flush(key, impl_->writer())) return false;
    auto reply = impl_->execute({"JSON.SET", key, path, value.dump()});
    if (path == "$" && reply && reply->find("WRONGTYPE") != std::string::npos) {
        auto deleted = impl_->execute({"DEL", key});
        if (error(deleted)) return false;
        reply = impl_->execute({"JSON.SET", key, path, value.dump()});
    }
    if (reply != "OK") return false;
    impl_->invalidate(key, path);
    return true;
}
bool MemoryClass::flush(const std::string& key) {
    auto guard = impl_->lock(key);
    return impl_->state->cache.flush(key, impl_->writer());
}
bool MemoryClass::erase(const std::string& key, const std::string& path) {
    auto guard = impl_->lock(key);
    std::string parent; std::vector<JsonPathSegment> relative;
    if (path != "$" && impl_->region(path, parent, relative) && !relative.empty()) {
        if (!impl_->state->cache.flush(key, impl_->writer())) return false;
    }
    std::vector<JsonPathSegment> segments;
    const bool shifts_array = parseJsonPath(path, segments) && std::any_of(segments.begin(), segments.end(),
        [](const auto& segment) { return segment.is_array_index; });
    if (shifts_array && !impl_->state->cache.flush(key, impl_->writer())) return false;
    auto reply = path == "$" ? impl_->execute({"DEL", key}) : impl_->execute({"JSON.DEL", key, path});
    if (error(reply)) return false;
    try { if (std::stoll(*reply) < 0) return false; } catch (...) { return false; }
    impl_->invalidate(key, shifts_array ? "$" : path);
    return true;
}
std::optional<std::string> MemoryClass::executeAtomic(const std::string& key,
        const std::vector<std::string>& command, const std::vector<std::string>& changed_paths) {
    auto guard = impl_->lock(key);
    if (!impl_->state->cache.flush(key, impl_->writer())) return {};
    auto reply = impl_->execute(command);
    for (const auto& path : changed_paths) impl_->invalidate(key, path);
    return error(reply) ? std::nullopt : reply;
}
bool MemoryClass::erasePattern(const std::string& pattern) {
    std::vector<std::unique_lock<std::mutex>> guards;
    for (auto& stripe : impl_->state->stripes) guards.emplace_back(stripe);
    auto keys = impl_->execute({"KEYS", pattern});
    if (!keys || (!keys->empty() && keys->front() == '\x01')) return false;
    std::istringstream stream(*keys); std::string key;
    std::vector<std::string> command{"DEL"};
    while (stream >> key) command.push_back(key);
    if (command.size() == 1) return true;
    auto reply = impl_->execute(command);
    if (error(reply)) return false;
    try { if (std::stoll(*reply) < 0) return false; } catch (...) { return false; }
    for (std::size_t index = 1; index < command.size(); ++index) impl_->state->cache.removeDocument(command[index]);
    return true;
}
