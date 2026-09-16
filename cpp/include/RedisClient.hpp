#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

class RedisClient {
public:
    RedisClient(const std::string& host = "127.0.0.1", int port = 6379, const std::string& user = "agora_user", const std::string& password = "AgoraUserSecret@Passw0rd!2026");
    ~RedisClient();

    bool connect();
    void disconnect();

    bool ping();
    bool set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
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
