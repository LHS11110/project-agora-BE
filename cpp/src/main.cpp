#include <iostream>
#include <string>
#include <cstdlib>
#include <csignal>
#include "CanvasPool.hpp"
#include "HttpServer.hpp"

static HttpServer* g_server = nullptr;

void signal_handler(int signal) {
    std::cout << "\n[Agora C++ Server] Caught signal " << signal << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string host = "0.0.0.0";
    int port = 8000;
    std::string db_host = "127.0.0.1";
    int db_port = 1433;
    std::string es_host = "127.0.0.1";
    int es_port = 9200;

    if (const char* env_host = std::getenv("HOST")) host = env_host;
    if (const char* env_port = std::getenv("PORT")) port = std::stoi(env_port);
    if (const char* env_db_host = std::getenv("DB_HOST")) db_host = env_db_host;
    if (const char* env_db_port = std::getenv("DB_PORT")) db_port = std::stoi(env_db_port);
    if (const char* env_es_host = std::getenv("ES_HOST")) es_host = env_es_host;
    if (const char* env_es_port = std::getenv("ES_PORT")) es_port = std::stoi(env_es_port);

    // Command line args override: ./agora_cpp_server [port] [host]
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    if (argc > 2) {
        host = argv[2];
    }

    std::cout << "========================================" << std::endl;
    std::cout << " Agora C++ Realtime Canvas Server" << std::endl;
    std::cout << " Listening on: " << host << ":" << port << std::endl;
    std::cout << " MSSQL:        " << db_host << ":" << db_port << std::endl;
    std::cout << " ES:           " << es_host << ":" << es_port << std::endl;
    std::cout << "========================================" << std::endl;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    CanvasPool pool(db_host, db_port, es_host, es_port);
    HttpServer server(pool, host, port);
    g_server = &server;

    server.start();

    std::cout << "[Agora C++ Server] Server stopped gracefully." << std::endl;
    return 0;
}
