#include "memory/MssqlMemory.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include "memory/SqlCommand.hpp"
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
thread_local DBINT lastSqlServerMessageNumber = 0;
thread_local int lastSqlServerMessageSeverity = 0;
thread_local int lastSqlServerMessageState = 0;
thread_local int lastSqlServerMessageLine = 0;

int captureSqlServerMessage(DBPROCESS*, DBINT message_number, int message_state,
                            int severity, char*, char*, char*, int line) {
    if (severity >= 11 && lastSqlServerMessageNumber == 0) {
        lastSqlServerMessageNumber = message_number;
        lastSqlServerMessageSeverity = severity;
        lastSqlServerMessageState = message_state;
        lastSqlServerMessageLine = line;
    }
    return 0;
}

std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    return environmentValue(name);
}

bool addRpcTextParameter(DBPROCESS* dbproc, const char* name, const std::string& value) {
    if (value.size() > SqlCommand::kMaxBoundTextBytes) return false;
    const DBINT byte_length = static_cast<DBINT>(value.size());
    // The login uses UTF-8; SQL parameter declarations below decide whether
    // the server treats each value as VARCHAR or NVARCHAR.
    // All parameters passed to sp_executesql are input-only. FreeTDS requires
    // maxlen=-1 for input RPC parameters; using the value's byte length here
    // makes dbrpcparam fail before the SQL reaches the server.
    return dbrpcparam(dbproc, name, 0, SYBVARCHAR, -1, byte_length,
                      reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))) == SUCCEED;
}

bool addRpcUnicodeParameter(DBPROCESS* dbproc, const char* name, const std::string& value) {
    if (value.size() > SqlCommand::kMaxStatementBytes) return false;
    const DBINT byte_length = static_cast<DBINT>(value.size());
    // SQL Server accepts NTEXT for sp_executesql's Unicode statement and
    // declaration parameters. This also supports statements longer than the
    // 4,000-character NVARCHAR limit.
    return dbrpcparam(dbproc, name, 0, SYBNTEXT, -1, byte_length,
                      reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))) == SUCCEED;
}

bool executeSql(DBPROCESS* dbproc, const SqlCommand& command) {
    if (!dbproc) return false;
    const std::string validation_error = command.validationError();
    if (!validation_error.empty()) {
        std::cerr << "[MssqlClient] Rejected invalid parameterized SQL command: "
                  << validation_error << ".\n";
        return false;
    }
    if (dbrpcinit(dbproc, "sp_executesql", 0) != SUCCEED) {
        std::cerr << "[MssqlClient] Could not initialize sp_executesql RPC.\n";
        return false;
    }

    const std::string declarations = command.parameterDeclarations();
    if (!addRpcUnicodeParameter(dbproc, "@stmt", command.statement())) {
        std::cerr << "[MssqlClient] Could not bind sp_executesql statement parameter.\n";
        return false;
    }
    if (!addRpcUnicodeParameter(dbproc, "@params", declarations)) {
        std::cerr << "[MssqlClient] Could not bind sp_executesql declaration parameter.\n";
        return false;
    }

    std::vector<DBINT> int_values;
    int_values.reserve(command.parameters().size());
    std::vector<DBBIGINT> bigint_values;
    bigint_values.reserve(command.parameters().size());
    for (const auto& parameter : command.parameters()) {
        if (parameter.type == SqlCommand::ParameterType::Int32) {
            int_values.push_back(static_cast<DBINT>(parameter.int_value));
        } else if (parameter.type == SqlCommand::ParameterType::Int64) {
            bigint_values.push_back(static_cast<DBBIGINT>(parameter.int64_value));
        }
    }

    std::size_t int_value_index = 0;
    std::size_t bigint_value_index = 0;
    for (const auto& parameter : command.parameters()) {
        if (parameter.type != SqlCommand::ParameterType::Int32
                && parameter.type != SqlCommand::ParameterType::Int64) {
            if (!addRpcTextParameter(dbproc, parameter.name.c_str(), parameter.text_value)) {
                std::cerr << "[MssqlClient] Could not bind sp_executesql text parameter.\n";
                return false;
            }
            continue;
        }

        if (parameter.type == SqlCommand::ParameterType::Int32) {
            DBINT& value = int_values[int_value_index++];
            if (dbrpcparam(dbproc, parameter.name.c_str(), 0, SYBINT4, -1, -1,
                          reinterpret_cast<BYTE*>(&value)) != SUCCEED) {
                std::cerr << "[MssqlClient] Could not bind sp_executesql integer parameter.\n";
                return false;
            }
        } else {
            DBBIGINT& value = bigint_values[bigint_value_index++];
            if (dbrpcparam(dbproc, parameter.name.c_str(), 0, SYBINT8, -1, -1,
                          reinterpret_cast<BYTE*>(&value)) != SUCCEED) {
                std::cerr << "[MssqlClient] Could not bind sp_executesql bigint parameter.\n";
                return false;
            }
        }
    }

    if (dbrpcsend(dbproc) != SUCCEED) {
        std::cerr << "[MssqlClient] Could not send sp_executesql RPC.\n";
        return false;
    }
    return true;
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
            dbmsghandle(captureSqlServerMessage);
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

MssqlMemory::MssqlMemory(const std::string& host, int port,
                         const std::string& user,
                         const std::string& pass,
                         const std::string& db)
    : host_(host), port_(port), user_(envOr("DB_USER", user)),
      pass_(envOr("DB_PASSWORD", pass)), db_(envOr("DB_NAME", db)) {
    MssqlConnectionPool::getInstance().init(host_, port_, user_, pass_, db_);
}

MssqlMemory::~MssqlMemory() {
}

namespace {
bool readSqlTextValue(DBPROCESS* dbproc, int column, std::optional<std::string>& value) {
    BYTE* source = dbdata(dbproc, column);
    if (!source) {
        value = std::nullopt;
        return true;
    }

    const DBINT source_length = dbdatlen(dbproc, column);
    if (source_length < 0) return false;
    if (source_length == 0) {
        value = std::string{};
        return true;
    }

    // Generic repositories are intended for API-sized values. Refuse oversized
    // cells instead of silently truncating an arbitrary database value.
    constexpr std::size_t max_cell_bytes = 16 * 1024 * 1024;
    const auto raw_length = static_cast<std::size_t>(source_length);
    if (raw_length > max_cell_bytes / 2) return false;
    const std::size_t capacity = std::max<std::size_t>(256, raw_length * 2 + 32);
    std::vector<BYTE> converted(capacity);
    const DBINT converted_length = dbconvert(
            dbproc, dbcoltype(dbproc, column), source, source_length,
            SYBVARCHAR, converted.data(), static_cast<DBINT>(converted.size()));
    if (converted_length < 0 || static_cast<std::size_t>(converted_length) > converted.size()) return false;

    std::size_t text_length = static_cast<std::size_t>(converted_length);
    if (text_length > 0 && converted[text_length - 1] == '\0') --text_length;
    value = std::string(reinterpret_cast<const char*>(converted.data()), text_length);
    return true;
}
}

std::optional<SqlQueryResult> MssqlMemory::query(const SqlCommand& command) {
    PooledConnection dbproc;
    if (!dbproc.get() || user_.empty() || pass_.empty() || db_.empty()) return std::nullopt;
    if (!executeSql(dbproc, command)) {
        (void)dbcancel(dbproc);
        return std::nullopt;
    }

    SqlQueryResult query_result;
    bool succeeded = true;
    RETCODE result_code;
    while ((result_code = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (result_code == FAIL) {
            succeeded = false;
            break;
        }

        const int column_count = dbnumcols(dbproc);
        SqlResultSet result_set;
        result_set.columns.reserve(static_cast<std::size_t>(std::max(column_count, 0)));
        for (int column = 1; column <= column_count; ++column) {
            const char* name = dbcolname(dbproc, column);
            result_set.columns.emplace_back(name ? name : "");
        }

        RETCODE row_code;
        while ((row_code = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (row_code == FAIL || row_code == BUF_FULL) {
                succeeded = false;
                break;
            }
            if (column_count <= 0) continue;

            SqlRow row;
            row.values.reserve(static_cast<std::size_t>(column_count));
            for (int column = 1; column <= column_count; ++column) {
                std::optional<std::string> value;
                if (!readSqlTextValue(dbproc, column, value)) {
                    succeeded = false;
                }
                row.values.push_back(std::move(value));
            }
            result_set.rows.push_back(std::move(row));
        }
        if (column_count > 0) query_result.result_sets.push_back(std::move(result_set));
        if (!succeeded) break;
    }

    if (!succeeded) {
        (void)dbcancel(dbproc);
        return std::nullopt;
    }
    return query_result;
}

bool MssqlMemory::execute(const SqlCommand& command) {
    PooledConnection dbproc;
    if (!dbproc.get() || user_.empty() || pass_.empty() || db_.empty()) return false;
    if (!executeSql(dbproc, command)) {
        (void)dbcancel(dbproc);
        return false;
    }

    bool succeeded = true;
    RETCODE result_code;
    while ((result_code = dbresults(dbproc)) != NO_MORE_RESULTS) {
        if (result_code == FAIL) {
            succeeded = false;
            break;
        }
        RETCODE row_code;
        while ((row_code = dbnextrow(dbproc)) != NO_MORE_ROWS) {
            if (row_code == FAIL || row_code == BUF_FULL) {
                succeeded = false;
                break;
            }
        }
        if (!succeeded) break;
    }
    if (!succeeded) (void)dbcancel(dbproc);
    return succeeded;
}

