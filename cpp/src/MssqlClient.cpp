#include "MssqlClient.hpp"
#include <iostream>
#include <sybfront.h>
#include <sybdb.h>
#include <cstring>
#include <httplib.h>
#include <mutex>
#include <vector>
#include <condition_variable>

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
        DBSETLUSER(login, user_.c_str());
        DBSETLPWD(login, pass_.c_str());
        DBSETLAPP(login, "AgoraCppServer");

        std::string server_str = host_ + ":" + std::to_string(port_);
        DBPROCESS* dbproc = dbopen(login, server_str.c_str());
        dbloginfree(login);

        if (dbproc) {
            dbuse(dbproc, db_.c_str());
        } else {
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
    : host_(host), port_(port), user_(user), pass_(pass), db_(db) {
    MssqlConnectionPool::getInstance().init(host, port, user, pass, db);
}

MssqlClient::~MssqlClient() {
}

bool MssqlClient::registerServer(const std::string& ip, int rest_port, int ws_port) {

    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "IF EXISTS (SELECT 1 FROM cpp_server WHERE server_ip = '" + ip + "' AND server_port = '" + std::to_string(rest_port) + "') "
        "BEGIN "
        "   UPDATE cpp_server SET ws_port = '" + std::to_string(ws_port) + "', is_activated = 1 WHERE server_ip = '" + ip + "' AND server_port = '" + std::to_string(rest_port) + "'; "
        "END "
        "ELSE "
        "BEGIN "
        "   INSERT INTO cpp_server (server_ip, server_port, ws_port, is_activated, created_at) VALUES ('" + ip + "', '" + std::to_string(rest_port) + "', '" + std::to_string(ws_port) + "', 1, GETDATE()); "
        "END; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
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

bool MssqlClient::unregisterServer(const std::string& ip, int rest_port) {
    PooledConnection dbproc;
    if (!dbproc.get()) return false;
    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "DELETE FROM cpp_server WHERE server_ip = '" + ip + "' AND server_port = '" + std::to_string(rest_port) + "'; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
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

bool MssqlClient::setServerInactive(const std::string& ip, int rest_port) {
    PooledConnection dbproc;
    if (!dbproc.get()) return false;
    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "UPDATE cpp_server SET is_activated = 0 WHERE server_ip = '" + ip + "' AND server_port = '" + std::to_string(rest_port) + "'; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
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
    if (!dbproc.get()) return {"127.0.0.1", 6379};

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "  DECLARE @is_cached BIT = NULL, @redis_ip NVARCHAR(50), @redis_port NVARCHAR(10), @assigned_cpp_id INT; "
        "  SELECT @is_cached = is_cached, @redis_ip = r.redis_ip, @redis_port = r.redis_port, @assigned_cpp_id = c.cpp_server_id "
        "    FROM canvas_info c WITH (UPDLOCK, ROWLOCK) "
        "    LEFT JOIN redis_server r ON c.redis_id = r.redis_id "
        "    WHERE c.canvas_id = " + std::to_string(canvasId) + "; "
        "  DECLARE @my_cpp_id INT; "
        "  SELECT @my_cpp_id = server_id FROM cpp_server WHERE server_ip = '" + cppServerIp + "' AND server_port = " + std::to_string(cppServerPort) + "; "
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
        "    IF @is_cached = 0 OR @redis_ip IS NULL "
        "    BEGIN "
        "      DECLARE @new_redis_id INT; "
        "      SELECT TOP 1 @new_redis_id = redis_id, @redis_ip = redis_ip, @redis_port = redis_port FROM redis_server WHERE is_activated = 1 ORDER BY NEWID(); "
        "      UPDATE canvas_info SET is_cached = 1, redis_id = @new_redis_id, cpp_server_id = @my_cpp_id, updated_at = SYSUTCDATETIME() WHERE canvas_id = " + std::to_string(canvasId) + "; "
        "    END "
        "    SELECT @redis_ip AS redis_ip, @redis_port AS redis_port; "
        "  END "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "END CATCH;";
    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
            return {"127.0.0.1", 6379};
    }

    char redis_ip_buf[64] = {0};
    char redis_port_buf[32] = {0};
    std::string found_ip = "127.0.0.1";
    int found_port = 6379;

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
                        found_port = 6379;
                    }
                }
            }
        }
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
        "UPDATE user_sessions SET is_accessed = 0, cpp_server_id = NULL, updated_at = SYSUTCDATETIME() WHERE user_id = " + std::to_string(userId) + "; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
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

    std::cout << "[MssqlClient] User #" << userId << " session in MS SQL updated: is_accessed=false, cpp_server_id=NULL\n";
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
        "  SELECT @server_id = server_id FROM cpp_server WHERE server_ip = '" + cppServerIp + "' AND server_port = " + std::to_string(cppServerPort) + "; "
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
        "  SELECT 0 AS success; "
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

int MssqlClient::getUserIdAndCheckWithdrawn(const std::string& nickname, int tagNumber) {
    PooledConnection pconn;
    DBPROCESS* dbproc = pconn.get();
    if (!dbproc) {
        std::cerr << "[MssqlClient] getUserIdAndCheckWithdrawn: Failed to get connection." << std::endl;
        return -1; 
    }

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "   SELECT user_id, status FROM users WHERE nickname = N'" + nickname + "' AND tag_number = " + std::to_string(tagNumber) + "; "
        "   COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "   IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "END CATCH;";
        
    if (dbcmd(dbproc, sql.c_str()) != SUCCEED) {
        std::cerr << "[MssqlClient] getUserIdAndCheckWithdrawn: dbcmd failed." << std::endl;
        return -1;
    }

    if (dbsqlexec(dbproc) != SUCCEED) {
        std::cerr << "[MssqlClient] getUserIdAndCheckWithdrawn: dbsqlexec failed." << std::endl;
        return -1;
    }

    int user_id = -1;
    // Wait, UserStatus in Java is Enum (ACTIVE, SUSPENDED, WITHDRAWN).
    // Usually stored as TINYINT in SQL Server if @Enumerated(EnumType.ORDINAL).
    bool withdrawn = false;
    
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        if (DBROWS(dbproc)) {
            DBINT id_val;
            char status_val[32] = {0};
            
            dbbind(dbproc, 1, INTBIND, 0, (BYTE*)&id_val);
            dbbind(dbproc, 2, NTBSTRINGBIND, 0, (BYTE*)status_val);

            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) break;
                user_id = id_val;
                if (strcmp(status_val, "WITHDRAWN") == 0) {
                    withdrawn = true;
                }
            }
        }
    }

    if (withdrawn) {
        std::cout << "[MssqlClient] getUserIdAndCheckWithdrawn: User is WITHDRAWN. id=" << user_id << std::endl;
        return -1;
    }

    return user_id;
}
