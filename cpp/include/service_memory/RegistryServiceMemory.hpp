#pragma once

#include <string>
#include <utility>
#include <optional>
#include <cstdint>
#include <vector>
#include "memory/SqlExecutor.hpp"
#include <memory>

struct CanvasStorageAssignment {
    bool is_cached{false};
    std::string cpp_server_ip;
    std::string cpp_server_port;
    std::string redis_ip;
    int redis_port{0};
};

struct RedisAllocationCandidate {
    std::string redis_ip;
    int redis_port{0};
    std::int64_t cached_canvas_count{0};
};

struct CanvasRedisAllocation {
    std::string redis_ip;
    int redis_port{0};
    bool was_cached{false};
    std::string allocation_strategy;
    std::vector<RedisAllocationCandidate> candidates;
    std::int64_t selected_cached_canvas_count{-1};
};

class RegistryServiceMemory final {
public:
    RegistryServiceMemory(const std::string& host = "127.0.0.1", int port = 1433,
                const std::string& user = "",
                const std::string& pass = "",
                const std::string& db = "");
    ~RegistryServiceMemory();

    explicit RegistryServiceMemory(std::unique_ptr<SqlExecutor> storage);

    // Register this server to DB (cpp_server table)
    bool registerServer(const std::string& ip, int rest_port, int ws_port);
    bool heartbeatServer(const std::string& ip, int rest_port);
    bool unregisterServer(const std::string& ip, int rest_port);
    bool setServerInactive(const std::string& ip, int rest_port);

    // Returns pair of <redis_ip, redis_port>. Allocates if not cached.
    CanvasRedisAllocation getOrAllocateRedisAndSetCached(int canvasId, const std::string& cppServerIp, int cppServerPort);

    // Updates canvas_info: is_cached=0, redis_ip=NULL, redis_port=NULL, server_ip=NULL, server_port=NULL
    bool updateCanvasUncached(int canvasId, const std::string& cppServerIp, int cppServerPort);

    // Resolve an active user from the nickname and tag in the canvas token.
    int getActiveUserId(const std::string& nickname, int tagNumber);
    std::optional<std::pair<std::string, int>> getUserHandle(int userId);
    bool isCanvasAssignedToServer(int canvasId, const std::string& serverIp, int serverPort);
    std::optional<CanvasStorageAssignment> getCanvasStorageAssignment(int canvasId);

private:
    std::unique_ptr<SqlExecutor> storage_;
};
