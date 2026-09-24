#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

class RedisClient {
public:
    enum class CompareSetResult { Applied, Conflict, Error };
    RedisClient(const std::string& host = "127.0.0.1", int port = 6379,
                const std::string& user = "", const std::string& password = "");
    ~RedisClient();

    bool connect();
    void disconnect();

    bool ping();
    bool set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
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
    bool del(const std::string& key);
    // Deletes the old cache only if a newer canvas load has not replaced it.
    CompareSetResult deleteIfCacheGenerationMatches(const std::string& key, const std::string& generation);
    bool deletePattern(const std::string& pattern);
    int getKeyCount(const std::string& pattern = "canvas*");

    // Helper to read JSON, update field, and write back
    bool updateJson(const std::string& key, const std::function<void(nlohmann::json&)>& modifier);

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    int socket_fd_;

    bool sendCommand(const std::vector<std::string>& args);
    std::string readResponse();
    std::string readLine();
};
