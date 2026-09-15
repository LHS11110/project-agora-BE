#include "WebSocketServer.hpp"
#include "App.h"
#include "RedisClient.hpp"
#include <iostream>
#include <ctime>
#include <nlohmann/json.hpp>
#include <httplib.h>

static std::string getQueryParam(std::string_view query, const std::string& key) {
    std::string q(query);
    std::string pattern = key + "=";
    auto pos = q.find(pattern);
    if (pos == std::string::npos) return "";
    auto end = q.find('&', pos);
    if (end == std::string::npos) return q.substr(pos + pattern.length());
    return q.substr(pos + pattern.length(), end - (pos + pattern.length()));
}

WebSocketServer::WebSocketServer(CanvasPool& pool, const std::string& host, int ws_port, TokenValidator validator,
                                 const std::string& java_host, int java_port)
    : pool_(pool), host_(host), ws_port_(ws_port), token_validator_(validator),
      java_host_(java_host), java_port_(java_port) {
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::start() {
    if (running_.exchange(true)) return;
    ws_thread_ = std::thread(&WebSocketServer::runServer, this);
}

void WebSocketServer::stop() {
    if (!running_.exchange(false)) return;
    if (loop_) {
        uWS::Loop::get()->defer([this]() {
            if (listen_socket_) {
                us_listen_socket_close(0, (struct us_listen_socket_t*)listen_socket_);
                listen_socket_ = nullptr;
            }
        });
    }
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    std::cout << "[uWebSockets] WebSocket server stopped gracefully." << std::endl;
}

void WebSocketServer::runServer() {
    loop_ = uWS::Loop::get();

    auto app = uWS::App();

    auto createWsHandler = [this]() {
        return uWS::App::WebSocketBehavior<PerSocketData>{
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 16 * 1024 * 1024,
            .idleTimeout = 120,
            .maxBackpressure = 1 * 1024 * 1024,
            .closeOnBackpressureLimit = false,
            .resetIdleTimeoutOnSend = false,
            .sendPingsAutomatically = true,

        .upgrade = [this](auto* res, auto* req, auto* context) {
            int canvas_id = 0;
            try {
                if (req->getParameter(0).length() > 0) {
                    canvas_id = std::stoi(std::string(req->getParameter(0)));
                }
            } catch (...) {}

            std::string query(req->getQuery());
            if (canvas_id <= 0) {
                std::string cid_str = getQueryParam(query, "canvas_id");
                if (cid_str.empty()) cid_str = getQueryParam(query, "canvasId");
                if (!cid_str.empty()) {
                    try { canvas_id = std::stoi(cid_str); } catch (...) {}
                }
            }

            int user_id = 1;
            std::string uid_str = getQueryParam(query, "user_id");
            if (uid_str.empty()) uid_str = getQueryParam(query, "userId");
            if (!uid_str.empty()) {
                try { user_id = std::stoi(uid_str); } catch (...) {}
            }

            std::string token = getQueryParam(query, "token");
            if (token_validator_ && !token.empty()) {
                int auth_uid = token_validator_(token);
                if (auth_uid > 0) user_id = auth_uid;
            }

            if (canvas_id <= 0) {
                res->writeStatus("400 Bad Request")->end("canvas_id is required");
                return;
            }

            res->template upgrade<PerSocketData>({
                canvas_id,
                user_id
            }, req->getHeader("sec-websocket-key"),
               req->getHeader("sec-websocket-protocol"),
               req->getHeader("sec-websocket-extensions"),
               context);
        },

        .open = [this](auto* ws) {
            PerSocketData* data = (PerSocketData*)ws->getUserData();
            std::cout << "[uWebSockets] WebSocket client connected: User #" << data->user_id
                      << " to Canvas #" << data->canvas_id << std::endl;

            // 1. Get or create canvas in pool (increments active load count)
            auto canvas = pool_.getOrCreateCanvas(data->canvas_id);
            if (canvas) {
                canvas->connectUser(data->user_id, 0, 0);

                // 2. Fetch cached items from Redis and send init_items JSON frame
                RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
                auto cached_str = redis.get("canvas:" + std::to_string(data->canvas_id));

                nlohmann::json init_msg = {
                    {"type", "init_items"},
                    {"canvas_id", data->canvas_id},
                    {"user_id", data->user_id},
                    {"server_protocol", "uWebSockets"},
                    {"status", "connected"},
                    {"items", nlohmann::json::object()}
                };

                if (cached_str && !cached_str->empty()) {
                    try {
                        auto doc = nlohmann::json::parse(*cached_str);
                        if (doc.contains("items")) init_msg["items"] = doc["items"];
                        if (doc.contains("inner-group")) init_msg["inner-group"] = doc["inner-group"];
                        if (doc.contains("canvas-name")) init_msg["canvas_name"] = doc["canvas-name"];
                    } catch (...) {}
                }

                ws->send(init_msg.dump(), uWS::OpCode::TEXT);
                std::cout << "[uWebSockets] Sent init_items to User #" << data->user_id
                          << " on Canvas #" << data->canvas_id << std::endl;
            }

            // Subscribe to canvas topic for pub/sub broadcasting
            ws->subscribe("canvas/" + std::to_string(data->canvas_id));
        },

        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            PerSocketData* data = (PerSocketData*)ws->getUserData();
            std::string text(message);

            // Handle ping/pong
            try {
                auto j = nlohmann::json::parse(text);
                if (j.value("type", "") == "ping") {
                    nlohmann::json pong = {
                        {"type", "pong"},
                        {"canvas_id", data->canvas_id},
                        {"user_id", data->user_id},
                        {"timestamp", (long long)time(nullptr)}
                    };
                    ws->send(pong.dump(), uWS::OpCode::TEXT);
                    return;
                }
            } catch (...) {}

            // Broadcast to other connected users on this canvas
            ws->publish("canvas/" + std::to_string(data->canvas_id), message, opCode);
        },

        .drain = [](auto* /*ws*/) {},

        .close = [this](auto* ws, int code, std::string_view /*message*/) {
            PerSocketData* data = (PerSocketData*)ws->getUserData();
            int canvas_id = data->canvas_id;
            int user_id = data->user_id;

            std::cout << "[uWebSockets] WebSocket client disconnected: User #" << user_id
                      << " from Canvas #" << canvas_id << " (close code: " << code << ")" << std::endl;

            // 1. Disconnect user from canvas in pool; unloads canvas if 0 active users remain (load -1)
            pool_.disconnectUser(canvas_id, user_id);

            // 2. Notify Java API to reflect user disconnect & session release
            std::string j_host = java_host_;
            int j_port = java_port_;
            std::thread([j_host, j_port, canvas_id, user_id]() {
                try {
                    httplib::Client cli(j_host, j_port);
                    cli.set_connection_timeout(2, 0);
                    cli.set_read_timeout(2, 0);
                    nlohmann::json body = {
                        {"canvas_id", canvas_id},
                        {"user_id", user_id}
                    };
                    auto res = cli.Post("/api/access/internal/disconnect", body.dump(), "application/json");
                    if (res && res->status == 200) {
                        std::cout << "[uWebSockets] Successfully reflected disconnect to Java API for User #"
                                  << user_id << " on Canvas #" << canvas_id << "\n";
                    } else {
                        std::cerr << "[uWebSockets] Java API disconnect reflection responded with status "
                                  << (res ? std::to_string(res->status) : "connection error") << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[uWebSockets] Failed to notify Java API of disconnect: " << e.what() << "\n";
                }
            }).detach();
        }
    };
};

    app.ws<PerSocketData>("/ws/canvas/:canvas_id", createWsHandler());
    app.ws<PerSocketData>("/ws/canvas", createWsHandler());

    app.listen(host_, ws_port_, [this](auto* token) {
        if (token) {
            std::cout << "[uWebSockets] Realtime WebSocket server listening on "
                      << host_ << ":" << ws_port_ << std::endl;
            listen_socket_ = token;
        } else {
            std::cerr << "[uWebSockets] Failed to listen on " << host_ << ":" << ws_port_ << std::endl;
            running_ = false;
        }
    });

    app.run();
}
