#include "MssqlClient.hpp"
#include <iostream>
#include <sybfront.h>
#include <sybdb.h>
#include <cstring>
#include <httplib.h>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <cstdlib>

namespace {
std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    const char* configured = std::getenv(name);
    return configured ? configured : "";
}

std::string sqlLiteral(std::string value) {
    std::size_t position = 0;
    while ((position = value.find('\'', position)) != std::string::npos) {
        value.insert(position, 1, '\'');
        position += 2;
    }
    return value;
}
}

class MssqlConnectionPool {
private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<DBPROCESS*> pool_;
    int active_connections_ = 0;
    const int max_connections_ = 10;

    std::string host_;
    int port_;
    std::string user_;
    std::string pass_;
    std::string db_;

public:
    static MssqlConnectionPool& getInstance() {
        static MssqlConnectionPool instance;
        return instance;
    }

    void init(const std::string& host, int port, const std::string& user, const std::string& pass, const std::string& db) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (host_.empty()) {
            host_ = host;
            port_ = port;
            user_ = user;
            pass_ = pass;
            db_ = db;
            dbinit();
            // Authentication runs on the uWebSockets event-loop thread.  Bound
            // DB waits prevent a database/network fault from stalling every
            // WebSocket handshake indefinitely.
            dbsetlogintime(5);
            dbsettime(5);
        }
    }

    DBPROCESS* acquire() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this]() { return !pool_.empty() || active_connections_ < max_connections_; });

        if (!pool_.empty()) {
            DBPROCESS* conn = pool_.back();
            pool_.pop_back();
            return conn;
        }

        active_connections_++;
        lock.unlock();

        LOGINREC* login = dblogin();
        if (!login) {
            std::cerr << "[MssqlClient] dblogin failed." << std::endl;
            lock.lock();
            active_connections_--;
            lock.unlock();
            cv_.notify_one();
            return nullptr;
        }
        DBSETLUSER(login, user_.c_str());
        DBSETLPWD(login, pass_.c_str());
        DBSETLAPP(login, "AgoraCppServer");
        // Token nicknames and SQL text are UTF-8. FreeTDS otherwise depends on
        // freetds.conf/LANG and may interpret Korean bytes as ISO-8859-1.
        if (DBSETLCHARSET(login, "UTF-8") != SUCCEED) {
            std::cerr << "[MssqlClient] Failed to configure UTF-8 client charset." << std::endl;
            dbloginfree(login);
            lock.lock();
            active_connections_--;
            lock.unlock();
            cv_.notify_one();
            return nullptr;
        }

        std::string server_str = host_ + ":" + std::to_string(port_);
        DBPROCESS* dbproc = dbopen(login, server_str.c_str());
        dbloginfree(login);

        if (dbproc && dbuse(dbproc, db_.c_str()) != SUCCEED) {
            std::cerr << "[MssqlClient] Failed to select database '" << db_ << "'." << std::endl;
            dbclose(dbproc);
            dbproc = nullptr;
        }
        if (!dbproc) {
            lock.lock();
            active_connections_--;
            lock.unlock();
            cv_.notify_one();
        }
        return dbproc;
    }

    void release(DBPROCESS* conn) {
        if (!conn) return;
        std::lock_guard<std::mutex> lock(mtx_);
        pool_.push_back(conn);
        cv_.notify_one();
    }
};

class PooledConnection {
    DBPROCESS* conn_;
public:
    PooledConnection() {
        conn_ = MssqlConnectionPool::getInstance().acquire();
    }
    ~PooledConnection() {
        MssqlConnectionPool::getInstance().release(conn_);
    }
    DBPROCESS* get() { return conn_; }
    operator DBPROCESS*() { return conn_; }
};

MssqlClient::MssqlClient(const std::string& host, int port,
                         const std::string& user,
                         const std::string& pass,
                         const std::string& db)
    : host_(host), port_(port), user_(envOr("DB_USER", user)),
      pass_(envOr("DB_PASSWORD", pass)), db_(envOr("DB_NAME", db)) {
    MssqlConnectionPool::getInstance().init(host_, port_, user_, pass_, db_);
}

MssqlClient::~MssqlClient() {
}

bool MssqlClient::registerServer(const std::string& ip, int rest_port, int ws_port) {

    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    if (user_.empty() || pass_.empty() || db_.empty()) {
        std::cerr << "[MssqlClient] DB_USER, DB_PASSWORD and DB_NAME must be configured\n";
        return false;
    }
    const std::string safe_ip = sqlLiteral(ip);
    std::string sql =
        "BEGIN TRAN; "
        "BEGIN TRY "
        "IF EXISTS (SELECT 1 FROM cpp_server WITH (UPDLOCK, HOLDLOCK) WHERE server_ip = '" + safe_ip + "' AND server_port = '" + std::to_string(rest_port) + "') "
        "BEGIN "
        "   UPDATE cpp_server SET ws_port = '" + std::to_string(ws_port) + "', is_activated = 1, last_heartbeat_at = SYSUTCDATETIME() WHERE server_ip = '" + safe_ip + "' AND server_port = '" + std::to_string(rest_port) + "'; "
        "END "
        "ELSE "
        "BEGIN "
        "   INSERT INTO cpp_server (server_ip, server_port, ws_port, is_activated, created_at, last_heartbeat_at) VALUES ('" + safe_ip + "', '" + std::to_string(rest_port) + "', '" + std::to_string(ws_port) + "', 1, SYSUTCDATETIME(), SYSUTCDATETIME()); "
        "END; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;";

    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
        return false;
    }

    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) return false;
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) break;
        }
    }

    return true;
}

bool MssqlClient::heartbeatServer(const std::string& ip, int rest_port) {
    PooledConnection dbproc;
    if (!dbproc.get()) return false;
    const std::string safe_ip = sqlLiteral(ip);
    std::string sql =
        "UPDATE cpp_server SET last_heartbeat_at = SYSUTCDATETIME(), is_activated = 1 "
        "WHERE server_ip = '" + safe_ip + "' AND server_port = '" + std::to_string(rest_port) + "'; "
        "SELECT @@ROWCOUNT AS affected;";
    dbcmd(dbproc, sql.c_str());
    if (dbsqlexec(dbproc) == FAIL) return false;

    int affected = 0;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) return false;
        if (DBROWS(dbproc)) {
            dbbind(dbproc, 1, INTBIND, 0, reinterpret_cast<BYTE*>(&affected));
            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) return false;
            }
        }
    }
    return affected == 1;
}

bool MssqlClient::unregisterServer(const std::string& ip, int rest_port) {
    return setServerInactive(ip, rest_port);
}

bool MssqlClient::setServerInactive(const std::string& ip, int rest_port) {
    PooledConnection dbproc;
    if (!dbproc.get()) return false;
    const std::string safe_ip = sqlLiteral(ip);
    std::string sql =
        "BEGIN TRAN; "
        "BEGIN TRY "
        "UPDATE cpp_server SET is_activated = 0, last_heartbeat_at = SYSUTCDATETIME() WHERE server_ip = '" + safe_ip + "' AND server_port = '" + std::to_string(rest_port) + "'; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;";
    dbcmd(dbproc, sql.c_str());
    if (dbsqlexec(dbproc) == FAIL) return false;
    
    bool res = true;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) res = false;
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) break;
        }
    }
    return res;
}

std::pair<std::string, int> MssqlClient::getOrAllocateRedisAndSetCached(int canvasId, const std::string& cppServerIp, int cppServerPort) {

    PooledConnection dbproc;
    if (!dbproc.get()) return {"ERROR", 0};

    const std::string safe_ip = sqlLiteral(cppServerIp);
    std::string sql =
        "BEGIN TRAN; "
        "BEGIN TRY "
        "  DECLARE @is_cached BIT = NULL, @redis_ip NVARCHAR(50), @redis_port NVARCHAR(10), @assigned_cpp_id INT; "
        "  SELECT @is_cached = is_cached, @redis_ip = r.redis_ip, @redis_port = r.redis_port, @assigned_cpp_id = c.cpp_server_id "
        "    FROM canvas_info c WITH (UPDLOCK, ROWLOCK) "
        "    LEFT JOIN redis_server r ON c.redis_id = r.redis_id AND r.is_activated = 1 "
        "    WHERE c.canvas_id = " + std::to_string(canvasId) + "; "
        "  DECLARE @my_cpp_id INT; "
        "  SELECT @my_cpp_id = server_id FROM cpp_server WHERE server_ip = '" + safe_ip + "' AND server_port = '" + std::to_string(cppServerPort) + "' AND is_activated = 1; "
        "  IF @my_cpp_id IS NULL THROW 50001, 'C++ server is not registered or active', 1; "
        "  IF @is_cached IS NULL "
        "  BEGIN "
        "    SELECT 'NOT_FOUND' AS redis_ip, '0' AS redis_port; "
        "  END "
        "  ELSE IF @is_cached = 1 AND @assigned_cpp_id IS NOT NULL AND @assigned_cpp_id != @my_cpp_id "
        "  BEGIN "
        "    SELECT 'WRONG_SERVER' AS redis_ip, '0' AS redis_port; "
        "  END "
        "  ELSE "
        "  BEGIN "
        "    IF @redis_ip IS NULL "
        "    BEGIN "
        "      DECLARE @new_redis_id INT; "
        "      SELECT TOP 1 @new_redis_id = redis_id, @redis_ip = redis_ip, @redis_port = redis_port FROM redis_server WHERE is_activated = 1 ORDER BY NEWID(); "
        "      IF @new_redis_id IS NULL THROW 50002, 'No active Redis server is available', 1; "
        "      UPDATE canvas_info SET redis_id = @new_redis_id WHERE canvas_id = " + std::to_string(canvasId) + "; "
        "    END "
        "    UPDATE canvas_info SET is_cached = 1, cpp_server_id = @my_cpp_id, updated_at = SYSUTCDATETIME() WHERE canvas_id = " + std::to_string(canvasId) + "; "
        "    SELECT @redis_ip AS redis_ip, @redis_port AS redis_port; "
        "  END "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;";
    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
        return {"ERROR", 0};
    }

    char redis_ip_buf[64] = {0};
    char redis_port_buf[32] = {0};
    std::string found_ip;
    int found_port = 0;

    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        if (DBROWS(dbproc)) {
            dbbind(dbproc, 1, NTBSTRINGBIND, 0, (BYTE*)redis_ip_buf);
            dbbind(dbproc, 2, NTBSTRINGBIND, 0, (BYTE*)redis_port_buf);

            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) break;
                if (strlen(redis_ip_buf) > 0) {
                    found_ip = redis_ip_buf;
                }
                if (strlen(redis_port_buf) > 0) {
                    try {
                        found_port = std::stoi(redis_port_buf);
                    } catch (...) {
                        found_port = 0;
                    }
                }
            }
        }
    }

    if (found_ip.empty() || found_port <= 0) {
        std::cerr << "[MssqlClient] Canvas #" << canvasId << " allocation returned no usable Redis endpoint\n";
        return {"ERROR", 0};
    }
    std::cout << "[MssqlClient] Canvas #" << canvasId << " assigned Redis from DB: " << found_ip << ":" << found_port << "\n";
    return {found_ip, found_port};
}

bool MssqlClient::updateCanvasUncached(int canvasId) {

    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "UPDATE canvas_info SET is_cached = 0, redis_id = NULL, cpp_server_id = NULL, updated_at = SYSUTCDATETIME() WHERE canvas_id = " + std::to_string(canvasId) + "; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;";
    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
        std::cerr << "[MssqlClient] Failed to execute updateCanvasUncached for canvas #" << canvasId << "\n";
            return false;
    }

    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) break;
        }
    }

    std::cout << "[MssqlClient] Canvas #" << canvasId << " in MS SQL updated: is_cached=false, redis/server ip&port=none(NULL)\n";
    return true;
}

bool MssqlClient::updateUserSessionDisconnected(int userId) {

    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "UPDATE user_sessions SET is_accessed = 0, cpp_server_id = NULL, canvas_id = NULL, updated_at = SYSUTCDATETIME() WHERE user_id = " + std::to_string(userId) + "; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;";
    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
        std::cerr << "[MssqlClient] Failed to execute updateUserSessionDisconnected for user #" << userId << "\n";
            return false;
    }

    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) break;
        }
    }

    std::cout << "[MssqlClient] User #" << userId << " session in MS SQL updated: is_accessed=false, cpp_server_id=NULL, canvas_id=NULL\n";
    return true;
}

bool MssqlClient::updateUserSessionConnected(int userId, int canvasId, const std::string& cppServerIp, int cppServerPort) {
    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "  DECLARE @current_accessed BIT, @current_canvas INT; "
        "  SELECT @current_accessed = is_accessed, @current_canvas = canvas_id "
        "    FROM user_sessions WITH (UPDLOCK, ROWLOCK) "
        "    WHERE user_id = " + std::to_string(userId) + "; "
        "  IF @current_accessed = 1 AND @current_canvas != " + std::to_string(canvasId) + " "
        "  BEGIN "
        "    ROLLBACK TRAN; "
        "    SELECT 0 AS success; "
        "    RETURN; "
        "  END "
        "  DECLARE @server_id INT; "
        "  SELECT @server_id = server_id FROM cpp_server WHERE server_ip = '" + sqlLiteral(cppServerIp) + "' AND server_port = '" + std::to_string(cppServerPort) + "' AND is_activated = 1; "
        "  IF @server_id IS NULL THROW 50003, 'C++ server session target is unavailable', 1; "
        "  UPDATE user_sessions SET is_accessed = 1, cpp_server_id = @server_id, canvas_id = " + std::to_string(canvasId) + ", updated_at = SYSUTCDATETIME() WHERE user_id = " + std::to_string(userId) + "; "
        "  IF @@ROWCOUNT = 0 "
        "  BEGIN "
        "    INSERT INTO user_sessions (user_id, canvas_id, cpp_server_id, is_accessed, updated_at) VALUES (" + std::to_string(userId) + ", " + std::to_string(canvasId) + ", @server_id, 1, SYSUTCDATETIME()); "
        "  END "
        "  COMMIT TRAN; "
        "  SELECT 1 AS success; "
        "END TRY "
        "BEGIN CATCH "
        "  IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "  THROW; "
        "END CATCH;";
        
    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
        std::cerr << "[MssqlClient] Failed to execute updateUserSessionConnected for user #" << userId << "\n";
        return false;
    }

    int success = 0;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        if (DBROWS(dbproc)) {
            char buf[16] = {0};
            dbbind(dbproc, 1, NTBSTRINGBIND, 0, (BYTE*)buf);
            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) break;
                if (strlen(buf) > 0) {
                    try {
                        success = std::stoi(buf);
                    } catch (...) {
                        success = 0;
                    }
                }
            }
        }
    }

    if (success == 0) {
        std::cerr << "[MssqlClient] User #" << userId << " already connected to another canvas. Connection rejected.\n";
        return false;
    }

    std::cout << "[MssqlClient] User #" << userId << " session in MS SQL updated: is_accessed=true, canvas_id=" << canvasId << "\n";
    return true;
}


bool MssqlClient::isCanvasActiveInDb(int canvasId) {
    PooledConnection pconn;
    DBPROCESS* dbproc = pconn.get();
    if (!dbproc) {
        std::cerr << "[MssqlClient] isCanvasActiveInDb: Failed to get connection." << std::endl;
        return true; // fail-safe, assume active
    }

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "   SELECT COUNT(*) FROM user_sessions WHERE canvas_id = " + std::to_string(canvasId) + " AND is_accessed = 1; "
        "   COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "   IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "   THROW; "
        "END CATCH;";
        
    if (dbcmd(dbproc, sql.c_str()) != SUCCEED) {
        std::cerr << "[MssqlClient] isCanvasActiveInDb: dbcmd failed." << std::endl;
        return true;
    }

    if (dbsqlexec(dbproc) != SUCCEED) {
        std::cerr << "[MssqlClient] isCanvasActiveInDb: dbsqlexec failed." << std::endl;
        return true;
    }

    int active_count = 0;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        if (DBROWS(dbproc)) {
            dbbind(dbproc, 1, INTBIND, 0, (BYTE*)&active_count);
            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) break;
            }
        }
    }

    return active_count > 0;
}

int MssqlClient::getActiveUserId(const std::string& nickname, int tagNumber) {
    if (nickname.empty() || tagNumber < 0) return -1;
    PooledConnection pconn;
    DBPROCESS* dbproc = pconn.get();
    if (!dbproc) {
        std::cerr << "[MssqlClient] getActiveUserId: Failed to get connection." << std::endl;
        return -1; 
    }

    std::string sql = "SELECT user_id FROM users WHERE nickname = N'" +
        sqlLiteral(nickname) + "' AND tag_number = " + std::to_string(tagNumber) +
        " AND status = 'ACTIVE';";
        
    if (dbcmd(dbproc, sql.c_str()) != SUCCEED) {
        std::cerr << "[MssqlClient] getActiveUserId: dbcmd failed." << std::endl;
        return -1;
    }

    if (dbsqlexec(dbproc) != SUCCEED) {
        std::cerr << "[MssqlClient] getActiveUserId: dbsqlexec failed." << std::endl;
        return -1;
    }

    int active_user_id = -1;
    
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) {
            std::cerr << "[MssqlClient] getActiveUserId: dbresults failed." << std::endl;
            return -1;
        }
        if (DBROWS(dbproc)) {
            DBINT id_val = -1;
            if (dbbind(dbproc, 1, INTBIND, 0, (BYTE*)&id_val) != SUCCEED) {
                std::cerr << "[MssqlClient] getActiveUserId: dbbind failed." << std::endl;
                return -1;
            }

            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) {
                    std::cerr << "[MssqlClient] getActiveUserId: dbnextrow failed." << std::endl;
                    return -1;
                }
                active_user_id = id_val;
            }
        }
    }

    return active_user_id;
}

std::optional<std::pair<std::string, int>> MssqlClient::getUserHandle(int userId) {
    if (userId <= 0) return std::nullopt;
    PooledConnection pconn;
    DBPROCESS* dbproc = pconn.get();
    if (!dbproc) return std::nullopt;
    std::string sql = "SELECT nickname, tag_number FROM users WHERE user_id = " + std::to_string(userId) + ";";
    if (dbcmd(dbproc, sql.c_str()) != SUCCEED || dbsqlexec(dbproc) != SUCCEED) return std::nullopt;
    std::optional<std::pair<std::string, int>> result;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) return std::nullopt;
        if (!DBROWS(dbproc)) continue;
        char nickname[512] = {0};
        DBINT tag = 0;
        if (dbbind(dbproc, 1, NTBSTRINGBIND, sizeof(nickname), reinterpret_cast<BYTE*>(nickname)) != SUCCEED
            || dbbind(dbproc, 2, INTBIND, 0, reinterpret_cast<BYTE*>(&tag)) != SUCCEED) return std::nullopt;
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) return std::nullopt;
            result = std::make_pair(std::string(nickname), static_cast<int>(tag));
        }
    }
    return result;
}

bool MssqlClient::isCanvasAssignedToServer(int canvasId, const std::string& serverIp, int serverPort) {
    if (canvasId <= 0 || serverPort <= 0) return false;
    PooledConnection pconn;
    DBPROCESS* dbproc = pconn.get();
    if (!dbproc) return false;
    std::string sql = "SELECT COUNT(*) FROM canvas_info c JOIN cpp_server s ON s.server_id = c.cpp_server_id "
        "WHERE c.canvas_id = " + std::to_string(canvasId) + " AND c.is_cached = 1 AND s.server_ip = '"
        + sqlLiteral(serverIp) + "' AND s.server_port = '" + std::to_string(serverPort) + "';";
    if (dbcmd(dbproc, sql.c_str()) != SUCCEED || dbsqlexec(dbproc) != SUCCEED) return false;
    int count = 0;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) return false;
        if (!DBROWS(dbproc)) continue;
        if (dbbind(dbproc, 1, INTBIND, 0, reinterpret_cast<BYTE*>(&count)) != SUCCEED) return false;
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) if (ret == FAIL) return false;
    }
    return count == 1;
}
