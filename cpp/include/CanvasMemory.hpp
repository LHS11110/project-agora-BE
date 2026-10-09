#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "KeyValueStore.hpp"

class RedisCommandExecutor;

// Storage policy boundary. Instances own independent transports while sharing
// the bounded process-wide LRU and per-document synchronization internally.
class CanvasMemory final : public KeyValueStore {
public:
    enum class CompareSetResult { Applied, Conflict, Error };
    CanvasMemory(const std::string& host = "127.0.0.1", int port = 6379,
                 const std::string& user = "", const std::string& password = "");
    explicit CanvasMemory(std::unique_ptr<RedisCommandExecutor> transport);
    ~CanvasMemory() override;
    CanvasMemory(const CanvasMemory&) = delete;
    CanvasMemory& operator=(const CanvasMemory&) = delete;
    bool connect();
    void disconnect();
    bool ping();

    std::optional<std::string> loadCanvas(int canvas_id);
    bool storeCanvas(int canvas_id, const nlohmann::json& document);
    bool flushCanvas(int canvas_id);
    bool clearCanvas(int canvas_id);
    CompareSetResult removeCanvasGeneration(int canvas_id, const std::string& generation);
    std::optional<std::string> readItem(int canvas_id, const std::string& item_id,
                                      const std::string& field = {});
    bool storeItem(int canvas_id, const std::string& item_id, const nlohmann::json& item);
    bool removeItem(int canvas_id, const std::string& item_id);
    bool replaceItems(int canvas_id, const nlohmann::json& items);
    bool appendMessage(int canvas_id, const std::string& room_id,
                       std::uint64_t sequence, const nlohmann::json& message);
    std::optional<std::string> readChatPage(int canvas_id, const std::string& room_id,
        const std::optional<std::uint64_t>& from, const std::optional<std::uint64_t>& to,
        std::uint64_t limit);
    CompareSetResult compareAndSetFields(int canvas_id, long long revision,
        const std::vector<std::pair<std::string, nlohmann::json>>& fields);

    // Generic key/value adapter and document-path operations for repositories.
    bool set(const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& key) override;
    bool flushCanvasItemCache(const std::string& key);
    std::optional<std::string> getJsonPath(const std::string& key, const std::string& path);
    bool setJsonPath(const std::string& key, const std::string& path, const nlohmann::json& value);
    bool appendChatMessage(const std::string& key, const std::string& item_id,
                           std::uint64_t sequence, const nlohmann::json& message);
    std::optional<std::string> getChatHistoryPage(const std::string& key, const std::string& item_id,
                           const std::optional<std::uint64_t>& from_sequence,
                           const std::optional<std::uint64_t>& to_sequence,
                           std::uint64_t limit);
    CompareSetResult compareAndSetJsonPaths(const std::string& key, long long expected_revision,
                            const std::vector<std::pair<std::string, nlohmann::json>>& values,
                            const std::vector<std::string>& deletes = {});
    bool deleteJsonPath(const std::string& key, const std::string& path);
    bool del(const std::string& key) override;
    // Deletes the old cache only if a newer canvas load has not replaced it.
    CompareSetResult deleteIfCacheGenerationMatches(const std::string& key, const std::string& generation);
    bool deletePattern(const std::string& pattern);
    int getKeyCount(const std::string& pattern = "canvas*");

    // Helper to read JSON, update field, and write back
    bool updateJson(const std::string& key, const std::function<void(nlohmann::json&)>& modifier);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool sendCommand(const std::vector<std::string>& command);
    std::string readResponse();
    std::optional<std::string> readFromRedis(const std::string& key);
    std::optional<std::string> readJsonPathFromRedis(const std::string& key, const std::string& path);
    bool writeBackCanvasItem(const std::string& key, const std::string& item_id,
                             const std::string& item_json);
    bool flushCanvasItemCacheLocked(const std::string& key);
};
