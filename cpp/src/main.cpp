#include <iostream>
#include <string>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include "CanvasPool.hpp"
#include "HttpServer.hpp"
#include "WebSocketServer.hpp"
#include "MssqlClient.hpp"

static HttpServer* g_server = nullptr;
static WebSocketServer* g_ws_server = nullptr;
static std::atomic<bool> g_cleanup_running{true};
static std::mutex g_cleanup_mutex;
static std::condition_variable g_cleanup_cv;

static std::string g_advertise_ip = "127.0.0.1";
static int g_port = 8000;
static std::string g_db_host = "127.0.0.1";
static int g_db_port = 1433;

void signal_handler(int signal) {
    std::cout << "\n[Agora C++ Server] Caught signal " << signal << ", shutting down..." << std::endl;
    
    int active_count = (g_server && g_server->getPool()) ? g_server->getPool()->getActiveCanvasCount() : 0;
    
    if (active_count > 0) {
        std::cout << "[Agora C++ Server] 이용중인 캔버스가 존재합니다. (Active: " << active_count << ")\n";
        std::cout << "[Agora C++ Server] 중단을 거절하고 is_activated=0으로 설정합니다.\n";
        MssqlClient mssql(g_db_host, g_db_port);
        mssql.setServerInactive(g_advertise_ip, g_port);
        return;
    }

    MssqlClient mssql(g_db_host, g_db_port);
    mssql.unregisterServer(g_advertise_ip, g_port);
    std::cout << "[Agora C++ Server] 서버 정보를 DB에서 삭제했습니다.\n";

    if (g_ws_server) {
        g_ws_server->stop();
    }
    if (g_server) {
        g_server->stop();
    }
    
    {
        std::lock_guard<std::mutex> lock(g_cleanup_mutex);
        g_cleanup_running = false;
    }
    g_cleanup_cv.notify_all();
}

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string host = "0.0.0.0";
    std::string es_host = "127.0.0.1";
    int es_port = 9200;
    std::string java_host = "127.0.0.1";
    int java_port = 8080;

    if (const char* env_host = std::getenv("HOST")) host = env_host;
    if (const char* env_adv_ip = std::getenv("ADVERTISE_IP")) g_advertise_ip = env_adv_ip;
    if (const char* env_port = std::getenv("PORT")) g_port = std::stoi(env_port);
    if (const char* env_db_host = std::getenv("DB_HOST")) g_db_host = env_db_host;
    if (const char* env_db_port = std::getenv("DB_PORT")) g_db_port = std::stoi(env_db_port);
    if (const char* env_es_host = std::getenv("ES_HOST")) es_host = env_es_host;
    if (const char* env_es_port = std::getenv("ES_PORT")) es_port = std::stoi(env_es_port);
    if (const char* env_java_host = std::getenv("JAVA_HOST")) java_host = env_java_host;
    if (const char* env_java_port = std::getenv("JAVA_PORT")) java_port = std::stoi(env_java_port);

    std::string jwt_secret = "testSecretKey~c29tZS12ZXJ5LXNlY3VyZS1hbmQtbG9uZy1zZWNyZXQta2V5LWZvci1hZ29yYS1qd3QtYXV0aC0yMDI2";
    if (const char* env_jwt_secret = std::getenv("JWT_SECRET")) jwt_secret = env_jwt_secret;

    int ws_port = g_port + 2;

    if (argc >= 5) {
        host = argv[1];
        g_advertise_ip = argv[2];
        g_port = std::stoi(argv[3]);
        ws_port = std::stoi(argv[4]);
    } else {
        if (argc > 1) host = argv[1];
        if (argc > 2) g_advertise_ip = argv[2];
        if (argc > 3) g_port = std::stoi(argv[3]);
        if (argc > 4) ws_port = std::stoi(argv[4]);
    }
    if (const char* env_ws_port = std::getenv("WS_PORT")) ws_port = std::stoi(env_ws_port);

    if (g_advertise_ip == "127.0.0.1" && host != "0.0.0.0") {
        g_advertise_ip = host;
    }
    if (g_advertise_ip == "127.0.0.1" && std::getenv("ADVERTISE_IP") == nullptr && argc <= 2) {
        g_advertise_ip = (host == "0.0.0.0" ? "127.0.0.1" : host);
    }

    std::cout << "========================================\n";
    std::cout << " Agora C++ Realtime Canvas Server\n";
    std::cout << " REST Listening on:      " << host << ":" << g_port << "\n";
    std::cout << " uWS WebSocket Port:     " << host << ":" << ws_port << "\n";
    std::cout << " MSSQL:                  " << g_db_host << ":" << g_db_port << "\n";
    std::cout << " ES:                     " << es_host << ":" << es_port << "\n";
    std::cout << " Java API:               " << java_host << ":" << java_port << "\n";
    std::cout << "----------------------------------------\n";
    std::cout << " [OSS Licenses & Attributions]\n";
    std::cout << " - uWebSockets & uSockets (Apache-2.0, (c) Alex Hultman)\n";
    std::cout << " - cpp-httplib & nlohmann/json (MIT)\n";
    std::cout << " - jwt-cpp (MIT, (c) Thalhammer)\n";
    std::cout << " - FreeTDS sybdb (LGPL-2.1+, see https://www.freetds.org/)\n";
    std::cout << " - zlib (zlib license)\n";
    std::cout << " See THIRD_PARTY_LICENSES.md for full license texts.\n";
    std::cout << "========================================\n";

    CanvasPool canvas_pool(g_db_host, g_db_port, es_host, es_port, java_host, java_port);

    MssqlClient mssql(g_db_host, g_db_port);
    if (!mssql.registerServer(g_advertise_ip, g_port, ws_port)) {
        std::cerr << "[MssqlClient] Failed to register server to DB.\n";
    } else {
        std::cout << "[MssqlClient] Successfully registered server to DB (IP: " << g_advertise_ip << ", REST: " << g_port << ", WS: " << ws_port << ")\n";
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    HttpServer server(canvas_pool, host, g_port, jwt_secret, g_db_host, g_db_port);
    WebSocketServer ws_server(canvas_pool, host, ws_port, [&](const std::string& token, int canvas_id, const std::string& client_ip) {
        return server.authenticateTokenForCanvas(token, canvas_id, client_ip, ws_port);
    }, java_host, java_port);

    g_server = &server;
    g_ws_server = &ws_server;

    std::thread cleanup_thread([&]() {
        while (g_cleanup_running) {
            std::cout << "[Agora C++ Server] Running 30-min canvas cleanup task...\n";
            canvas_pool.cleanupInactiveCanvases();

            std::unique_lock<std::mutex> lock(g_cleanup_mutex);
            g_cleanup_cv.wait_for(lock, std::chrono::minutes(30), [] { return !g_cleanup_running; });
        }
    });

    ws_server.start();
    server.start();

    {
        std::lock_guard<std::mutex> lock(g_cleanup_mutex);
        g_cleanup_running = false;
    }
    g_cleanup_cv.notify_all();
    if (cleanup_thread.joinable()) {
        cleanup_thread.join();
    }

    std::cout << "[Agora C++ Server] Server stopped gracefully." << std::endl;
    return 0;
}
