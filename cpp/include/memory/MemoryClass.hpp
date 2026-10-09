#pragma once
#include "memory/RedisCommandExecutor.hpp"
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>

// Generic RedisJSON memory. Cache admission, write-back and eviction are
// implementation details. Share State only between connections to one backend.
class MemoryClass final {
public:
    struct Options {
        std::size_t maximum_documents = 256;
        std::size_t entries_per_document = 64;
        std::size_t maximum_entry_bytes = 256 * 1024;
        std::size_t region_depth = 0; // 0 caches whole documents; N caches subtrees.
        // Optional fields kept remotely (for example unbounded collections).
        std::function<std::vector<std::string>(const nlohmann::json&)> remote_fields;
    };
    struct State;
    static std::shared_ptr<State> createState(Options options);
    MemoryClass(const std::string& host = "127.0.0.1", int port = 6379,
                const std::string& user = {}, const std::string& password = {},
                std::shared_ptr<State> state = {});
    explicit MemoryClass(std::unique_ptr<RedisCommandExecutor> transport,
                         std::shared_ptr<State> state = {});
    ~MemoryClass();
    bool connect();
    void disconnect();
    bool ping();
    std::optional<std::string> read(const std::string& key);
    std::optional<std::string> read(const std::string& key, const std::string& path);
    bool write(const std::string& key, const nlohmann::json& value, const std::string& path = "$");
    bool erase(const std::string& key, const std::string& path = "$");
    bool erasePattern(const std::string& pattern);
    bool flush(const std::string& key);
    // Opaque atomic storage operation: flush before execution, invalidate
    // affected regions afterwards, including uncertain transport outcomes.
    std::optional<std::string> executeAtomic(const std::string& key,
        const std::vector<std::string>& command, const std::vector<std::string>& changed_paths = {"$"});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
