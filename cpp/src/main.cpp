#include <iostream>
#include <string>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <chrono>
#include "CanvasPool.hpp"
#include "HttpServer.hpp"
#include "WebSocketServer.hpp"
#include "MssqlClient.hpp"

static HttpServer* g_server = nullptr;
static WebSocketServer* g_ws_server = nullptr;

void signal_handler(int signal) {
    std::cout << "\n[Agora C++ Server] Caught signal " << signal << ", shutting down..." << std::endl;
    if (g_ws_server) {
        g_ws_server->stop();
    }
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string host = "0.0.0.0";
    int port = 8000;
    std::string db_host = "127.0.0.1";
    int db_port = 1433;
    std::string es_host = "127.0.0.1";
    int es_port = 9200;
    std::string java_host = "127.0.0.1";
    int java_port = 8080;

    if (const char* env_host = std::getenv("HOST")) host = env_host;
    if (const char* env_port = std::getenv("PORT")) port = std::stoi(env_port);
    if (const char* env_db_host = std::getenv("DB_HOST")) db_host = env_db_host;
    if (const char* env_db_port = std::getenv("DB_PORT")) db_port = std::stoi(env_db_port);
    if (const char* env_es_host = std::getenv("ES_HOST")) es_host = env_es_host;
    if (const char* env_es_port = std::getenv("ES_PORT")) es_port = std::stoi(env_es_port);
    if (const char* env_java_host = std::getenv("JAVA_HOST")) java_host = env_java_host;
    if (const char* env_java_port = std::getenv("JAVA_PORT")) java_port = std::stoi(env_java_port);

    // Command line args override: ./agora_cpp_server [port] [host]
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    if (argc > 2) {
        host = argv[2];
    }
    int ws_port = port + 2; // Default to 8002 if port is 8000, avoiding 8001 (Redis Stack)
    if (const char* env_ws_port = std::getenv("WS_PORT")) ws_port = std::stoi(env_ws_port);

    std::cout << "========================================" << std::endl;
    std::cout << " Agora C++ Realtime Canvas Server" << std::endl;
    std::cout << " REST Listening on:      " << host << ":" << port << std::endl;
    std::cout << " uWS WebSocket Port:     " << host << ":" << ws_port << std::endl;
    std::cout << " MSSQL:                  " << db_host << ":" << db_port << std::endl;
    std::cout << " ES:                     " << es_host << ":" << es_port << std::endl;
    std::cout << " Java API:               " << java_host << ":" << java_port << std::endl;
    std::cout << "========================================" << std::endl;

    MssqlClient dbClient(db_host, db_port);
    if (dbClient.registerServer(host == "0.0.0.0" ? "127.0.0.1" : host, port, ws_port)) {
        std::cout << "[MssqlClient] Successfully registered server to DB (IP: " << (host == "0.0.0.0" ? "127.0.0.1" : host) << ", REST: " << port << ", WS: " << ws_port << ")\n";
    } else {
        std::cerr << "[MssqlClient] Failed to register server to DB!\n";
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    CanvasPool pool(db_host, db_port, es_host, es_port, java_host, java_port);
    HttpServer server(pool, host, port);
    WebSocketServer ws_server(pool, host, ws_port, [&](const std::string& token, int canvas_id) {
        return server.authenticateTokenForCanvas(token, canvas_id);
    }, java_host, java_port);

    g_server = &server;
    g_ws_server = &ws_server;

    ws_server.start();
    server.start();

    std::cout << "[Agora C++ Server] Server stopped gracefully." << std::endl;
    return 0;
}
