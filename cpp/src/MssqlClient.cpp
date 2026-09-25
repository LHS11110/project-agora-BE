#include "MssqlClient.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include "SqlCommand.hpp"
#include "Environment.hpp"
#include <iostream>
#include <sybfront.h>
#include <sybdb.h>
#include <algorithm>
#include <cstring>
#include <httplib.h>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <cstdlib>
#include <thread>
#include <chrono>

namespace {
std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    return environmentValue(name);
}

bool addRpcTextParameter(DBPROCESS* dbproc, const char* name, const std::string& value) {
    if (value.size() > SqlCommand::kMaxRpcTextBytes) return false;
    const DBINT byte_length = static_cast<DBINT>(value.size());
    const DBINT max_length = std::max<DBINT>(byte_length, 1);
    // The login uses UTF-8; SQL parameter declarations below decide whether
    // the server treats each value as VARCHAR or NVARCHAR.
    return dbrpcparam(dbproc, name, 0, SYBVARCHAR, max_length, byte_length,
                      reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))) == SUCCEED;
}

bool executeSql(DBPROCESS* dbproc, const SqlCommand& command) {
    if (!dbproc || !command.isValid() || dbrpcinit(dbproc, "sp_executesql", 0) != SUCCEED) return false;

    const std::string declarations = command.parameterDeclarations();
    if (!addRpcTextParameter(dbproc, "@stmt", command.statement())
        || !addRpcTextParameter(dbproc, "@params", declarations)) {
        return false;
    }

    std::vector<DBINT> int_values;
    int_values.reserve(command.parameters().size());
    for (const auto& parameter : command.parameters()) {
        if (parameter.type == SqlCommand::ParameterType::Int32) {
            int_values.push_back(static_cast<DBINT>(parameter.int_value));
        }
    }

    std::size_t int_value_index = 0;
    for (const auto& parameter : command.parameters()) {
        if (parameter.type != SqlCommand::ParameterType::Int32) {
            if (!addRpcTextParameter(dbproc, parameter.name.c_str(), parameter.text_value)) return false;
            continue;
        }

        DBINT& value = int_values[int_value_index++];
        if (dbrpcparam(dbproc, parameter.name.c_str(), 0, SYBINT4,
                      static_cast<DBINT>(sizeof(value)), static_cast<DBINT>(sizeof(value)),
                      reinterpret_cast<BYTE*>(&value)) != SUCCEED) {
            return false;
        }
    }

    return dbrpcsend(dbproc) == SUCCEED;
}

std::string readSqlServerName(DBPROCESS* dbproc) {
    SqlCommand command("SELECT CONVERT(NVARCHAR(128), @@SERVERNAME) AS server_name;");
    if (!executeSql(dbproc, command)) return {};

    std::string server_name;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) return {};
        if (!DBROWS(dbproc)) continue;
        char value[256] = {0};
        if (dbbind(dbproc, 1, NTBSTRINGBIND, sizeof(value), reinterpret_cast<BYTE*>(value)) != SUCCEED) return {};
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) return {};
            server_name = value;
        }
    }
    return server_name;
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
    bool tls_configuration_valid_ = true;

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
            const std::string configured_encrypt = environmentValue("DB_ENCRYPT");
            const std::string encrypt = configured_encrypt.empty() ? "true" : configured_encrypt;
            const std::string trust_certificate = environmentValue("DB_TRUST_SERVER_CERTIFICATE");
            const std::string freetds_config = envOr("DB_FREETDS_CONF", "");
            if (encrypt == "true" && trust_certificate != "true") {
                if (freetds_config.empty()) {
                    tls_configuration_valid_ = false;
                    std::cerr << "[MssqlClient] DB_ENCRYPT=true requires DB_FREETDS_CONF with strict TLS and certificate validation\n";
                } else if (setenv("FREETDSCONF", freetds_config.c_str(), 1) != 0) {
                    tls_configuration_valid_ = false;
                    std::cerr << "[MssqlClient] Could not set FreeTDS configuration path\n";
                }
            } else if (!freetds_config.empty()) {
                setenv("FREETDSCONF", freetds_config.c_str(), 1);
            }
            dbinit();
            // Authentication runs on the uWebSockets event-loop thread.  Bound
            // DB waits prevent a database/network fault from stalling every
            // WebSocket handshake indefinitely.
            dbsetlogintime(3);
            dbsettime(5);
        }
    }

    DBPROCESS* acquire() {
        if (!tls_configuration_valid_) return nullptr;
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this]() { return !pool_.empty() || active_connections_ < max_connections_; });

        while (!pool_.empty()) {
            DBPROCESS* conn = pool_.back();
            pool_.pop_back();
            if (!DBDEAD(conn)) return conn;
            dbclose(conn);
            --active_connections_;
            ElasticsearchBulkLogBuffer::instance().reportAvailability(
                    "mssql-ag", false, {{"listener", host_ + ":" + std::to_string(port_)}});
        }

        active_connections_++;
        lock.unlock();

        DBPROCESS* dbproc = openConnectionWithRetry();
        if (!dbproc) {
            lock.lock();
            active_connections_--;
            lock.unlock();
            cv_.notify_all();
        }
        return dbproc;
    }

    void release(DBPROCESS* conn) {
        if (!conn) return;
        std::lock_guard<std::mutex> lock(mtx_);
        if (DBDEAD(conn)) {
            // A failover breaks pooled TCP sessions. Never hand a dead session
            // to the next request; the next acquisition opens through DB_HOST,
            // which is configured as the AG listener.
            dbclose(conn);
            --active_connections_;
            ElasticsearchBulkLogBuffer::instance().reportAvailability(
                    "mssql-ag", false, {{"listener", host_ + ":" + std::to_string(port_)}});
            cv_.notify_all();
            return;
        }
        pool_.push_back(conn);
        cv_.notify_one();
    }

private:
    DBPROCESS* openConnectionWithRetry() {
        const std::string server_str = host_ + ":" + std::to_string(port_);
        int failed_attempts = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            LOGINREC* login = dblogin();
            if (!login) {
                std::cerr << "[MssqlClient] dblogin failed." << std::endl;
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
                return nullptr;
            }

            DBPROCESS* dbproc = dbopen(login, server_str.c_str());
            dbloginfree(login);
            if (dbproc && dbuse(dbproc, db_.c_str()) != SUCCEED) {
                std::cerr << "[MssqlClient] Failed to select database '" << db_ << "'." << std::endl;
                dbclose(dbproc);
                dbproc = nullptr;
            }
            if (dbproc) {
                const bool recovered = ElasticsearchBulkLogBuffer::instance().reportAvailability(
                        "mssql-ag", true, {{"listener", server_str}});
                const std::string primary = readSqlServerName(dbproc);
                if (!primary.empty()) {
                    ElasticsearchBulkLogBuffer::instance().reportPrimaryChange("mssql-ag", primary);
                } else {
                    ElasticsearchBulkLogBuffer::instance().record(
                            "mssql-ag", "primary_probe_failed", "WARN",
                            "Connected to the SQL Server listener but could not read the current primary name",
                            {{"listener", server_str}});
                }
                if (failed_attempts > 0 && !recovered) {
                    ElasticsearchBulkLogBuffer::instance().record(
                            "mssql-ag", "connection_retry_succeeded", "WARN",
                            "SQL Server listener connection succeeded after a retry",
                            {{"listener", server_str}, {"failed_attempts", failed_attempts}});
                }
                return dbproc;
            }

            ++failed_attempts;
            if (attempt < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200 * (1 << attempt)));
            }
        }
        ElasticsearchBulkLogBuffer::instance().reportAvailability(
                "mssql-ag", false,
                {{"listener", server_str}, {"failed_attempts", failed_attempts}});
        std::cerr << "[MssqlClient] Could not connect to the SQL Server listener after bounded retries." << std::endl;
        return nullptr;
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
    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "IF EXISTS (SELECT 1 FROM cpp_server WITH (UPDLOCK, HOLDLOCK) WHERE server_ip = @ip AND server_port = @rest_port) "
        "BEGIN "
        "   UPDATE cpp_server SET ws_port = @ws_port, is_activated = 1, last_heartbeat_at = SYSUTCDATETIME() WHERE server_ip = @ip AND server_port = @rest_port; "
        "END "
        "ELSE "
        "BEGIN "
        "   INSERT INTO cpp_server (server_ip, server_port, ws_port, is_activated, created_at, last_heartbeat_at) VALUES (@ip, @rest_port, @ws_port, 1, SYSUTCDATETIME(), SYSUTCDATETIME()); "
        "END; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addVarchar("@ip", ip).addVarchar("@rest_port", std::to_string(rest_port))
       .addVarchar("@ws_port", std::to_string(ws_port));

    if (!executeSql(dbproc, sql)) {
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
    SqlCommand sql(
        "UPDATE cpp_server SET last_heartbeat_at = SYSUTCDATETIME(), is_activated = 1 "
        "WHERE server_ip = @ip AND server_port = @rest_port; "
        "SELECT @@ROWCOUNT AS affected;");
    sql.addVarchar("@ip", ip).addVarchar("@rest_port", std::to_string(rest_port));
    if (!executeSql(dbproc, sql)) return false;

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
    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "UPDATE cpp_server SET is_activated = 0, last_heartbeat_at = SYSUTCDATETIME() WHERE server_ip = @ip AND server_port = @rest_port; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addVarchar("@ip", ip).addVarchar("@rest_port", std::to_string(rest_port));
    if (!executeSql(dbproc, sql)) return false;
    
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

CanvasRedisAllocation MssqlClient::getOrAllocateRedisAndSetCached(int canvasId, const std::string& cppServerIp, int cppServerPort) {

    PooledConnection dbproc;
    if (!dbproc.get()) return {"ERROR", 0, false};

    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "  DECLARE @is_cached BIT = NULL, @was_cached BIT = 0, @redis_ip NVARCHAR(50), @redis_port NVARCHAR(10), @assigned_cpp_id INT; "
        "  SELECT @is_cached = is_cached, @was_cached = is_cached, @redis_ip = r.redis_ip, @redis_port = r.redis_port, @assigned_cpp_id = c.cpp_server_id "
        "    FROM canvas_info c WITH (UPDLOCK, ROWLOCK) "
        "    LEFT JOIN redis_server r ON c.redis_id = r.redis_id AND r.is_activated = 1 "
        "    WHERE c.canvas_id = @canvas_id; "
        "  DECLARE @my_cpp_id INT; "
        "  SELECT @my_cpp_id = server_id FROM cpp_server WHERE server_ip = @cpp_ip AND server_port = @cpp_port AND is_activated = 1; "
        "  IF @my_cpp_id IS NULL THROW 50001, 'C++ server is not registered or active', 1; "
        "  IF @is_cached IS NULL "
        "  BEGIN "
        "    SELECT 'NOT_FOUND' AS redis_ip, '0' AS redis_port, '0' AS was_cached; "
        "  END "
        "  ELSE IF @is_cached = 1 AND @assigned_cpp_id IS NOT NULL AND @assigned_cpp_id != @my_cpp_id "
        "  BEGIN "
        "    SELECT 'WRONG_SERVER' AS redis_ip, '0' AS redis_port, '0' AS was_cached; "
        "  END "
        "  ELSE IF @is_cached = 1 AND @redis_ip IS NULL "
        "  BEGIN "
        "    SELECT 'ERROR' AS redis_ip, '0' AS redis_port, '1' AS was_cached; "
        "  END "
        "  ELSE "
        "  BEGIN "
        "    IF @redis_ip IS NULL "
        "    BEGIN "
        "      DECLARE @new_redis_id INT; "
        "      SELECT TOP 1 @new_redis_id = redis_id, @redis_ip = redis_ip, @redis_port = redis_port FROM redis_server WHERE is_activated = 1 ORDER BY NEWID(); "
        "      IF @new_redis_id IS NULL THROW 50002, 'No active Redis server is available', 1; "
        "      UPDATE canvas_info SET redis_id = @new_redis_id WHERE canvas_id = @canvas_id; "
        "    END "
        "    UPDATE canvas_info SET is_cached = 1, cpp_server_id = @my_cpp_id, updated_at = SYSUTCDATETIME() WHERE canvas_id = @canvas_id; "
        "    SELECT @redis_ip AS redis_ip, @redis_port AS redis_port, CONVERT(VARCHAR(5), @was_cached) AS was_cached; "
        "  END "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addInt("@canvas_id", canvasId).addVarchar("@cpp_ip", cppServerIp)
       .addVarchar("@cpp_port", std::to_string(cppServerPort));

    if (!executeSql(dbproc, sql)) {
        return {"ERROR", 0, false};
    }

    char redis_ip_buf[64] = {0};
    char redis_port_buf[32] = {0};
    char was_cached_buf[16] = {0};
    std::string found_ip;
    int found_port = 0;
    bool was_cached = false;

    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        if (DBROWS(dbproc)) {
            dbbind(dbproc, 1, NTBSTRINGBIND, 0, (BYTE*)redis_ip_buf);
            dbbind(dbproc, 2, NTBSTRINGBIND, 0, (BYTE*)redis_port_buf);
            dbbind(dbproc, 3, NTBSTRINGBIND, sizeof(was_cached_buf), (BYTE*)was_cached_buf);

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
                was_cached = std::string(was_cached_buf) == "1";
            }
        }
    }

    if (found_ip.empty() || found_port <= 0) {
        std::cerr << "[MssqlClient] Canvas #" << canvasId << " allocation returned no usable Redis endpoint\n";
        return {"ERROR", 0, was_cached};
    }
    std::cout << "[MssqlClient] Canvas #" << canvasId << " assigned Redis from DB: " << found_ip << ":" << found_port << "\n";
    return {found_ip, found_port, was_cached};
}

bool MssqlClient::updateCanvasUncached(int canvasId, const std::string& cppServerIp, int cppServerPort) {

    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "DECLARE @server_id INT, @affected INT = 0, @is_cached BIT, @assigned_server_id INT, @has_active_session BIT = 0; "
        "SELECT @server_id = server_id FROM cpp_server WHERE server_ip = @cpp_ip AND server_port = @cpp_port; "
        "SELECT @is_cached = is_cached, @assigned_server_id = cpp_server_id "
        "FROM canvas_info WITH (UPDLOCK, HOLDLOCK, ROWLOCK) WHERE canvas_id = @canvas_id; "
        "IF @is_cached = 1 AND @assigned_server_id = @server_id "
        "BEGIN "
        "  SELECT TOP 1 @has_active_session = 1 FROM user_sessions WITH (UPDLOCK, ROWLOCK) "
        "  WHERE canvas_id = @canvas_id AND is_accessed = 1; "
        "  IF @has_active_session = 0 "
        "  BEGIN "
        "    UPDATE canvas_info SET is_cached = 0, redis_id = NULL, cpp_server_id = NULL, updated_at = SYSUTCDATETIME() "
        "    WHERE canvas_id = @canvas_id AND is_cached = 1 AND cpp_server_id = @server_id; "
        "    SET @affected = @@ROWCOUNT; "
        "  END "
        "END "
        "COMMIT TRAN; "
        "SELECT @affected AS affected; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addInt("@canvas_id", canvasId).addVarchar("@cpp_ip", cppServerIp)
       .addVarchar("@cpp_port", std::to_string(cppServerPort));

    if (!executeSql(dbproc, sql)) {
        std::cerr << "[MssqlClient] Failed to execute updateCanvasUncached for canvas #" << canvasId << "\n";
            return false;
    }

    int affected = 0;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) break;
        if (DBROWS(dbproc)) {
            dbbind(dbproc, 1, INTBIND, 0, reinterpret_cast<BYTE*>(&affected));
            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) break;
            }
        }
    }

    if (affected != 1) {
        std::cerr << "[MssqlClient] Canvas #" << canvasId
                  << " was not uncached because its assignment changed or an active session exists\n";
        return false;
    }

    std::cout << "[MssqlClient] Canvas #" << canvasId << " in MS SQL updated: is_cached=false, redis/server ip&port=none(NULL)\n";
    return true;
}

bool MssqlClient::updateUserSessionDisconnected(int userId, int canvasId,
                                                const std::string& cppServerIp, int cppServerPort) {

    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "DECLARE @server_id INT; "
        "SELECT @server_id = server_id FROM cpp_server WHERE server_ip = @cpp_ip AND server_port = @cpp_port; "
        "UPDATE user_sessions SET is_accessed = 0, cpp_server_id = NULL, canvas_id = NULL, updated_at = SYSUTCDATETIME() "
        "WHERE user_id = @user_id AND canvas_id = @canvas_id AND is_accessed = 1 AND cpp_server_id = @server_id; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addVarchar("@cpp_ip", cppServerIp).addVarchar("@cpp_port", std::to_string(cppServerPort))
       .addInt("@user_id", userId).addInt("@canvas_id", canvasId);

    if (!executeSql(dbproc, sql)) {
        std::cerr << "[MssqlClient] Failed to execute updateUserSessionDisconnected for user #" << userId << "\n";
            return false;
    }

    bool read_ok = true;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) { read_ok = false; break; }
        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) { read_ok = false; break; }
        }
        if (!read_ok) break;
    }

    if (!read_ok) return false;

    std::cout << "[MssqlClient] User #" << userId << " session in MS SQL updated: is_accessed=false, cpp_server_id=NULL, canvas_id=NULL\n";
    return true;
}

bool MssqlClient::updateUserSessionConnected(int userId, int canvasId, const std::string& cppServerIp, int cppServerPort) {
    PooledConnection dbproc;
    if (!dbproc.get()) return false;

    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "  DECLARE @canvas_exists INT, @canvas_cached BIT, @canvas_cpp_id INT; "
        "  SELECT @canvas_exists = 1, @canvas_cached = is_cached, @canvas_cpp_id = cpp_server_id "
        "    FROM canvas_info WITH (UPDLOCK, HOLDLOCK, ROWLOCK) WHERE canvas_id = @canvas_id; "
        "  IF @canvas_exists IS NULL THROW 50004, 'Canvas does not exist', 1; "
        "  DECLARE @server_id INT; "
        "  SELECT @server_id = server_id FROM cpp_server WHERE server_ip = @cpp_ip AND server_port = @cpp_port AND is_activated = 1; "
        "  IF @server_id IS NULL THROW 50003, 'C++ server session target is unavailable', 1; "
        "  IF @canvas_cached <> 1 OR @canvas_cpp_id IS NULL OR @canvas_cpp_id <> @server_id THROW 50005, 'Canvas is assigned to another C++ server', 1; "
        "  DECLARE @current_accessed BIT, @current_canvas INT; "
        "  SELECT @current_accessed = is_accessed, @current_canvas = canvas_id "
        "    FROM user_sessions WITH (UPDLOCK, HOLDLOCK, ROWLOCK) "
        "    WHERE user_id = @user_id; "
        "  IF @current_accessed = 1 AND (@current_canvas IS NULL OR @current_canvas <> @canvas_id) "
        "  BEGIN "
        "    ROLLBACK TRAN; "
        "    SELECT 0 AS success; "
        "    RETURN; "
        "  END "
        "  UPDATE user_sessions SET is_accessed = 1, cpp_server_id = @server_id, canvas_id = @canvas_id, updated_at = SYSUTCDATETIME() WHERE user_id = @user_id; "
        "  IF @@ROWCOUNT = 0 "
        "  BEGIN "
        "    INSERT INTO user_sessions (user_id, canvas_id, cpp_server_id, is_accessed, updated_at) VALUES (@user_id, @canvas_id, @server_id, 1, SYSUTCDATETIME()); "
        "  END "
        "  COMMIT TRAN; "
        "  SELECT 1 AS success; "
        "END TRY "
        "BEGIN CATCH "
        "  IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "  THROW; "
        "END CATCH;");
    sql.addInt("@canvas_id", canvasId).addVarchar("@cpp_ip", cppServerIp)
       .addVarchar("@cpp_port", std::to_string(cppServerPort)).addInt("@user_id", userId);
        
    if (!executeSql(dbproc, sql)) {
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

    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "   SELECT COUNT(*) FROM user_sessions WHERE canvas_id = @canvas_id AND is_accessed = 1; "
        "   COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "   IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "   THROW; "
        "END CATCH;");
    sql.addInt("@canvas_id", canvasId);
        
    if (!executeSql(dbproc, sql)) {
        std::cerr << "[MssqlClient] isCanvasActiveInDb: query execution failed." << std::endl;
        return true;
    }

    int active_count = 0;
    bool count_read = false;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) return true;
        if (DBROWS(dbproc)) {
            if (dbbind(dbproc, 1, INTBIND, 0, (BYTE*)&active_count) != SUCCEED) return true;
            while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
                if (ret == FAIL) return true;
                count_read = true;
            }
        }
    }
    if (!count_read) return true;
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

    SqlCommand sql("SELECT user_id FROM users WHERE nickname = @nickname AND tag_number = @tag AND status = 'ACTIVE';");
    sql.addText("@nickname", nickname).addInt("@tag", tagNumber);
        
    if (!executeSql(dbproc, sql)) {
        std::cerr << "[MssqlClient] getActiveUserId: query execution failed." << std::endl;
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
    SqlCommand sql("SELECT nickname, tag_number FROM users WHERE user_id = @user_id;");
    sql.addInt("@user_id", userId);
    if (!executeSql(dbproc, sql)) return std::nullopt;
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
    SqlCommand sql("SELECT COUNT(*) FROM canvas_info c JOIN cpp_server s ON s.server_id = c.cpp_server_id "
                   "WHERE c.canvas_id = @canvas_id AND c.is_cached = 1 AND s.server_ip = @server_ip AND s.server_port = @server_port;");
    sql.addInt("@canvas_id", canvasId).addVarchar("@server_ip", serverIp)
       .addVarchar("@server_port", std::to_string(serverPort));
    if (!executeSql(dbproc, sql)) return false;
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

std::optional<CanvasStorageAssignment> MssqlClient::getCanvasStorageAssignment(int canvasId) {
    if (canvasId <= 0) return std::nullopt;
    PooledConnection pconn;
    DBPROCESS* dbproc = pconn.get();
    if (!dbproc) return std::nullopt;

    SqlCommand sql(
        "SELECT CONVERT(VARCHAR(5), c.is_cached), "
        "COALESCE(s.server_ip, ''), COALESCE(CONVERT(VARCHAR(16), s.server_port), ''), "
        "COALESCE(r.redis_ip, ''), COALESCE(CONVERT(VARCHAR(16), r.redis_port), '') "
        "FROM canvas_info c "
        "LEFT JOIN cpp_server s ON s.server_id = c.cpp_server_id "
        "LEFT JOIN redis_server r ON r.redis_id = c.redis_id AND r.is_activated = 1 "
        "WHERE c.canvas_id = @canvas_id;");
    sql.addInt("@canvas_id", canvasId);
    if (!executeSql(dbproc, sql)) return std::nullopt;

    std::optional<CanvasStorageAssignment> assignment;
    RETCODE ret;
    while ((ret = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (ret == FAIL) return std::nullopt;
        if (!DBROWS(dbproc)) continue;

        char cached[16] = {0};
        char server_ip[128] = {0};
        char server_port[32] = {0};
        char redis_ip[128] = {0};
        char redis_port[32] = {0};
        if (dbbind(dbproc, 1, NTBSTRINGBIND, sizeof(cached), reinterpret_cast<BYTE*>(cached)) != SUCCEED
            || dbbind(dbproc, 2, NTBSTRINGBIND, sizeof(server_ip), reinterpret_cast<BYTE*>(server_ip)) != SUCCEED
            || dbbind(dbproc, 3, NTBSTRINGBIND, sizeof(server_port), reinterpret_cast<BYTE*>(server_port)) != SUCCEED
            || dbbind(dbproc, 4, NTBSTRINGBIND, sizeof(redis_ip), reinterpret_cast<BYTE*>(redis_ip)) != SUCCEED
            || dbbind(dbproc, 5, NTBSTRINGBIND, sizeof(redis_port), reinterpret_cast<BYTE*>(redis_port)) != SUCCEED) {
            return std::nullopt;
        }

        while ((ret = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (ret == FAIL) return std::nullopt;
            CanvasStorageAssignment value;
            value.is_cached = std::string(cached) == "1";
            value.cpp_server_ip = server_ip;
            value.cpp_server_port = server_port;
            value.redis_ip = redis_ip;
            if (redis_port[0] != '\0') {
                try {
                    value.redis_port = std::stoi(redis_port);
                } catch (...) {
                    return std::nullopt;
                }
            }
            assignment = std::move(value);
        }
    }
    return assignment;
}
