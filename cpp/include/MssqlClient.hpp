#pragma once

#include <string>
#include <utility>

class MssqlClient {
public:
    MssqlClient(const std::string& host = "127.0.0.1", int port = 1433,
                const std::string& user = "agora_user",
                const std::string& pass = "AgoraUserSecret@Passw0rd!2026",
                const std::string& db = "agora_db");
    ~MssqlClient();

    // Register this server to DB (cpp_server table)
    bool registerServer(const std::string& ip, int rest_port, int ws_port);
    bool unregisterServer(const std::string& ip, int rest_port);
    bool setServerInactive(const std::string& ip, int rest_port);

    // Returns pair of <redis_ip, redis_port>. Allocates if not cached.
    std::pair<std::string, int> getOrAllocateRedisAndSetCached(int canvasId, const std::string& cppServerIp, int cppServerPort);

    // Updates canvas_info: is_cached=0, redis_ip=NULL, redis_port=NULL, server_ip=NULL, server_port=NULL
    bool updateCanvasUncached(int canvasId);

    // Updates user_sessions: is_accessed=0, cpp_server_id=NULL
    bool updateUserSessionDisconnected(int userId);

    // Updates user_sessions: is_accessed=1, cpp_server_id=(subquery), canvas_id=? (UPSERT)
    bool updateUserSessionConnected(int userId, int canvasId, const std::string& cppServerIp, int cppServerPort);

    // Checks if the canvas has any active sessions
    bool isCanvasActiveInDb(int canvasId);

    // Checks if the user is deleted (status = 'WITHDRAWN')
    int getUserIdAndCheckWithdrawn(const std::string& nickname, int tagNumber);

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string pass_;
    std::string db_;
};
