#include "MssqlClient.hpp"
#include <iostream>
#include <sybfront.h>
#include <sybdb.h>
#include <cstring>
#include <httplib.h>

MssqlClient::MssqlClient(const std::string& host, int port,
                         const std::string& user,
                         const std::string& pass,
                         const std::string& db)
    : host_(host), port_(port), user_(user), pass_(pass), db_(db) {
}

MssqlClient::~MssqlClient() {
}

std::pair<std::string, int> MssqlClient::getAssignedRedis(int canvasId) {
    static bool dbinit_done = false;
    if (!dbinit_done) {
        dbinit();
        dbinit_done = true;
    }

    LOGINREC *login = dblogin();
    if (!login) {
        return {"127.0.0.1", 6379};
    }

    DBSETLUSER(login, user_.c_str());
    DBSETLPWD(login, pass_.c_str());
    DBSETLAPP(login, "AgoraCppServer");

    std::string server_str = host_ + ":" + std::to_string(port_);
    DBPROCESS *dbproc = dbopen(login, server_str.c_str());
    dbloginfree(login);

    if (!dbproc) {
        std::cerr << "[MssqlClient] Direct dbopen failed to " << server_str << ", trying default 127.0.0.1:6379\n";
        return {"127.0.0.1", 6379};
    }

    if (dbuse(dbproc, db_.c_str()) == FAIL) {
        dbclose(dbproc);
        return {"127.0.0.1", 6379};
    }

    std::string sql = "SELECT redis_ip, redis_port FROM canvas_info WHERE canvas_id = " + std::to_string(canvasId);
    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
        dbclose(dbproc);
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

    dbclose(dbproc);
    std::cout << "[MssqlClient] Canvas #" << canvasId << " assigned Redis from DB: " << found_ip << ":" << found_port << "\n";
    return {found_ip, found_port};
}

bool MssqlClient::updateCanvasUncached(int canvasId) {
    static bool dbinit_done = false;
    if (!dbinit_done) {
        dbinit();
        dbinit_done = true;
    }

    LOGINREC *login = dblogin();
    if (!login) {
        std::cerr << "[MssqlClient] dblogin failed in updateCanvasUncached\n";
        return false;
    }

    DBSETLUSER(login, user_.c_str());
    DBSETLPWD(login, pass_.c_str());
    DBSETLAPP(login, "AgoraCppServer");

    std::string server_str = host_ + ":" + std::to_string(port_);
    DBPROCESS *dbproc = dbopen(login, server_str.c_str());
    dbloginfree(login);

    if (!dbproc) {
        std::cerr << "[MssqlClient] dbopen failed in updateCanvasUncached to " << server_str << "\n";
        return false;
    }

    if (dbuse(dbproc, db_.c_str()) == FAIL) {
        dbclose(dbproc);
        return false;
    }

    std::string sql = "UPDATE canvas_info SET is_cached = 0, redis_ip = NULL, redis_port = NULL, server_ip = NULL, server_port = NULL, updated_at = SYSUTCDATETIME() WHERE canvas_id = " + std::to_string(canvasId);
    dbcmd(dbproc, sql.c_str());

    if (dbsqlexec(dbproc) == FAIL) {
        std::cerr << "[MssqlClient] Failed to execute updateCanvasUncached for canvas #" << canvasId << "\n";
        dbclose(dbproc);
        return false;
    }

    while (dbresults(dbproc) != NO_MORE_RESULTS) {
        // consume result sets if any
    }

    dbclose(dbproc);
    std::cout << "[MssqlClient] Canvas #" << canvasId << " in MS SQL updated: is_cached=false, redis/server ip&port=none(NULL)\n";
    return true;
}

