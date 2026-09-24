#pragma once

#include <string>
#include <utility>
#include <optional>

struct CanvasStorageAssignment {
    bool is_cached{false};
    std::string cpp_server_ip;
    std::string cpp_server_port;
    std::string redis_ip;
    int redis_port{0};
};

struct CanvasRedisAllocation {
    std::string redis_ip;
    int redis_port{0};
    bool was_cached{false};
};

class MssqlClient {
public:
    MssqlClient(const std::string& host = "127.0.0.1", int port = 1433,
                const std::string& user = "",
                const std::string& pass = "",
                const std::string& db = "");
    ~MssqlClient();

    // Register this server to DB (cpp_server table)
    bool registerServer(const std::string& ip, int rest_port, int ws_port);
    bool heartbeatServer(const std::string& ip, int rest_port);
    bool unregisterServer(const std::string& ip, int rest_port);
    bool setServerInactive(const std::string& ip, int rest_port);

    // Returns pair of <redis_ip, redis_port>. Allocates if not cached.
    CanvasRedisAllocation getOrAllocateRedisAndSetCached(int canvasId, const std::string& cppServerIp, int cppServerPort);

    // Updates canvas_info: is_cached=0, redis_ip=NULL, redis_port=NULL, server_ip=NULL, server_port=NULL
    bool updateCanvasUncached(int canvasId, const std::string& cppServerIp, int cppServerPort);

    // Updates user_sessions after the user's final connection closes.
    bool updateUserSessionDisconnected(int userId, int canvasId,
                                       const std::string& cppServerIp, int cppServerPort);

    // Updates user_sessions: is_accessed=1, cpp_server_id=(subquery), canvas_id=? (UPSERT)
    bool updateUserSessionConnected(int userId, int canvasId, const std::string& cppServerIp, int cppServerPort);

    // Checks if the canvas has any active sessions
    bool isCanvasActiveInDb(int canvasId);

    // Resolve an active user from the nickname and tag in the canvas token.
    int getActiveUserId(const std::string& nickname, int tagNumber);
    std::optional<std::pair<std::string, int>> getUserHandle(int userId);
    bool isCanvasAssignedToServer(int canvasId, const std::string& serverIp, int serverPort);
    std::optional<CanvasStorageAssignment> getCanvasStorageAssignment(int canvasId);

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string pass_;
    std::string db_;
};
