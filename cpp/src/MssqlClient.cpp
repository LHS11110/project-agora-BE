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
        "   INSERT INTO cpp_server (server_ip, server_port, ws_port, is_activated) VALUES ('" + ip + "', '" + std::to_string(rest_port) + "', '" + std::to_string(ws_port) + "', 1); "
        "END; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "END CATCH;";

    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL || dbresults(dbproc) == FAIL) {
            return false;
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
    bool res = (dbsqlexec(dbproc) != FAIL && dbresults(dbproc) != FAIL);
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
    bool res = (dbsqlexec(dbproc) != FAIL && dbresults(dbproc) != FAIL);
    return res;
}

std::pair<std::string, int> MssqlClient::getAssignedRedis(int canvasId) {

    PooledConnection dbproc;
    if (!dbproc.get()) return {"127.0.0.1", 6379};

    std::string sql = 
        "BEGIN TRAN; "
        "BEGIN TRY "
        "SELECT r.redis_ip, r.redis_port FROM canvas_info c JOIN redis_server r ON c.redis_id = r.redis_id WHERE c.canvas_id = " + std::to_string(canvasId) + "; "
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

    while (dbresults(dbproc) != NO_MORE_RESULTS) {
        dbbind(dbproc, 1, NTBSTRINGBIND, 0, (BYTE*)redis_ip_buf);
        dbbind(dbproc, 2, NTBSTRINGBIND, 0, (BYTE*)redis_port_buf);

        while (dbnextrow(dbproc) != NO_MORE_ROWS) {
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

    while (dbresults(dbproc) != NO_MORE_RESULTS) {
        // consume result sets if any
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

    while (dbresults(dbproc) != NO_MORE_RESULTS) {
        // consume result sets if any
    }

    std::cout << "[MssqlClient] User #" << userId << " session in MS SQL updated: is_accessed=false, cpp_server_id=NULL\n";
    return true;
}
