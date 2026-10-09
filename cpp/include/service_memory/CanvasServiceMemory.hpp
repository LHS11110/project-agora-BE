#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

class RedisCommandExecutor;
class MemoryClass;
class CanvasSnapshotMemory;

// Canvas keys, domain operations and durable-document restoration.
// Generic caching and transport policy belong exclusively to MemoryClass.
class CanvasServiceMemory final {
public:
    enum class CompareSetResult { Applied, Conflict, Error };
    CanvasServiceMemory(const std::string& host = "127.0.0.1", int port = 6379,
                 const std::string& user = "", const std::string& password = "");
    explicit CanvasServiceMemory(std::unique_ptr<RedisCommandExecutor> transport);
    explicit CanvasServiceMemory(std::unique_ptr<MemoryClass> memory);
    ~CanvasServiceMemory();
    std::optional<nlohmann::json> initializeCanvas(int id, CanvasSnapshotMemory& snapshots,
        bool already_cached, const std::string& generation);
    CanvasServiceMemory(const CanvasServiceMemory&) = delete;
    CanvasServiceMemory& operator=(const CanvasServiceMemory&) = delete;
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

    bool patchItemField(int canvas_id, const std::string& item_id, const std::string& field,
                        const nlohmann::json& value);
    bool clearItems(int canvas_id);

private:
    std::unique_ptr<MemoryClass> memory_;
    bool appendChatMessage(const std::string&, const std::string&, std::uint64_t, const nlohmann::json&);
    std::optional<std::string> getChatHistoryPage(const std::string&, const std::string&,
        const std::optional<std::uint64_t>&, const std::optional<std::uint64_t>&, std::uint64_t);
    CompareSetResult compareAndSetJsonPaths(const std::string&, long long,
        const std::vector<std::pair<std::string, nlohmann::json>>&, const std::vector<std::string>& = {});
    CompareSetResult deleteIfCacheGenerationMatches(const std::string&, const std::string&);
};
