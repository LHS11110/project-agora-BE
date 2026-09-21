#include "WebSocketServer.hpp"
#include "RedisClient.hpp"
#include <ctime>
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

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
    : pool_(pool), host_(host), ws_port_(ws_port), token_validator_(std::move(validator)),
      java_host_(java_host), java_port_(java_port) {
    pool_.setWebSocketCallbacks({
        [this](int canvas_id, const nlohmann::json& data, int exclude_user_id) {
            broadcastToCanvas(canvas_id, data, exclude_user_id);
        },
        [this](int canvas_id, int user_id, const nlohmann::json& data) {
            sendToUser(canvas_id, user_id, data);
        },
        [this](int canvas_id, int user_id) {
            disconnectUser(canvas_id, user_id);
        },
        [this](int canvas_id) {
            disconnectCanvas(canvas_id);
        }
    });
}

WebSocketServer::~WebSocketServer() {
    stop();
    pool_.setWebSocketCallbacks({});
}

void WebSocketServer::start() {
    if (running_.exchange(true)) return;
    ws_thread_ = std::thread(&WebSocketServer::runServer, this);
}

void WebSocketServer::stop() {
    const bool was_running = running_.exchange(false);

    if (was_running) {
        std::lock_guard<std::mutex> lock(loop_mutex_);
        if (loop_) {
            loop_->defer([this]() {
                std::vector<Socket*> sockets;
                for (const auto& [canvas_id, canvas_sockets] : sockets_by_canvas_) {
                    sockets.insert(sockets.end(), canvas_sockets.begin(), canvas_sockets.end());
                }
                sockets_by_canvas_.clear();

                for (Socket* ws : sockets) {
                    ws->end(1001, "Server shutting down");
                }
                if (listen_socket_) {
                    us_listen_socket_close(0, static_cast<us_listen_socket_t*>(listen_socket_));
                    listen_socket_ = nullptr;
                }
            });
        }
    }

    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(loop_mutex_);
        loop_ = nullptr;
        listen_socket_ = nullptr;
    }

    if (was_running) {
        std::cout << "[uWebSockets] WebSocket server stopped gracefully." << std::endl;
    }
}

void WebSocketServer::broadcastToCanvas(int canvas_id, const nlohmann::json& data, int exclude_user_id) {
    std::string payload = data.dump();

    std::lock_guard<std::mutex> lock(loop_mutex_);
    if (!running_ || !loop_) return;

    loop_->defer([this, canvas_id, exclude_user_id, payload = std::move(payload)]() {
        auto it = sockets_by_canvas_.find(canvas_id);
        if (it == sockets_by_canvas_.end()) return;

        for (Socket* ws : it->second) {
            PerSocketData* data = ws->getUserData();
            if (exclude_user_id > 0 && data->user_id == exclude_user_id) continue;
            ws->send(payload, uWS::OpCode::TEXT);
        }
    });
}

void WebSocketServer::sendToUser(int canvas_id, int user_id, const nlohmann::json& data) {
    std::string payload = data.dump();

    std::lock_guard<std::mutex> lock(loop_mutex_);
    if (!running_ || !loop_) return;

    loop_->defer([this, canvas_id, user_id, payload = std::move(payload)]() {
        auto it = sockets_by_canvas_.find(canvas_id);
        if (it == sockets_by_canvas_.end()) return;

        for (Socket* ws : it->second) {
            if (ws->getUserData()->user_id == user_id) {
                ws->send(payload, uWS::OpCode::TEXT);
            }
        }
    });
}

void WebSocketServer::disconnectUser(int canvas_id, int user_id) {
    std::lock_guard<std::mutex> lock(loop_mutex_);
    if (!running_ || !loop_) return;

    loop_->defer([this, canvas_id, user_id]() {
        auto it = sockets_by_canvas_.find(canvas_id);
        if (it == sockets_by_canvas_.end()) return;

        std::vector<Socket*> sockets_to_close;
        for (Socket* ws : it->second) {
            if (ws->getUserData()->user_id == user_id) {
                sockets_to_close.push_back(ws);
            }
        }
        for (Socket* ws : sockets_to_close) {
            ws->end(1008, "Access revoked");
        }
    });
}

void WebSocketServer::disconnectCanvas(int canvas_id) {
    std::lock_guard<std::mutex> lock(loop_mutex_);
    if (!running_ || !loop_) return;

    loop_->defer([this, canvas_id]() {
        auto it = sockets_by_canvas_.find(canvas_id);
        if (it == sockets_by_canvas_.end()) return;

        std::vector<Socket*> sockets_to_close(it->second.begin(), it->second.end());
        for (Socket* ws : sockets_to_close) {
            ws->end(1008, "Canvas session ended");
        }
    });
}

void WebSocketServer::registerSocket(Socket* ws) {
    const PerSocketData* data = ws->getUserData();
    sockets_by_canvas_[data->canvas_id].insert(ws);
}

void WebSocketServer::unregisterSocket(Socket* ws) {
    const PerSocketData* data = ws->getUserData();
    auto it = sockets_by_canvas_.find(data->canvas_id);
    if (it == sockets_by_canvas_.end()) return;

    it->second.erase(ws);
    if (it->second.empty()) {
        sockets_by_canvas_.erase(it);
    }
}

void WebSocketServer::runServer() {
    {
        std::lock_guard<std::mutex> lock(loop_mutex_);
        loop_ = uWS::Loop::get();
    }
    if (!running_) {
        std::lock_guard<std::mutex> lock(loop_mutex_);
        loop_ = nullptr;
        return;
    }

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

            if (canvas_id <= 0) {
                std::cout << "[uWebSockets] Upgrade rejected: 400 Bad Request (canvas_id is required)" << std::endl;
                res->writeStatus("400 Bad Request")->end("canvas_id is required");
                return;
            }

            int user_id = -1;
            std::string token = getQueryParam(query, "token");
            if (token_validator_ && !token.empty()) {
                std::string client_ip = std::string(res->getRemoteAddressAsText());
                user_id = token_validator_(token, canvas_id, client_ip);
            }

            if (user_id <= 0) {
                std::cout << "[uWebSockets] Upgrade rejected: 401 Unauthorized (Invalid, missing, or unauthorized JWT token for canvas #"
                          << canvas_id << ")" << std::endl;
                res->writeStatus("401 Unauthorized")->end("Invalid, missing, or unauthorized JWT token");
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
            PerSocketData* data = ws->getUserData();
            std::cout << "[uWebSockets] WebSocket client connected: User #" << data->user_id
                      << " to Canvas #" << data->canvas_id << std::endl;

            registerSocket(ws);
            ws->subscribe("canvas/" + std::to_string(data->canvas_id));
            if (!pool_.updateUserSessionConnected(data->user_id, data->canvas_id)) {
                std::cerr << "[WebSocketServer] Rejecting connection: User already in another canvas\n";
                ws->end(1008, "Already connected to another canvas");
                return;
            }

            // Get or create canvas in pool (increments active connection count).
            auto canvas = pool_.getOrCreateCanvas(data->canvas_id);
            if (!canvas) {
                ws->end(1011, "Canvas initialization failed");
                return;
            }
            canvas->connectUser(data->user_id, 0, 0);

            // Fetch cached items from Redis and send init_items JSON frame.
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
        },

        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            PerSocketData* data = ws->getUserData();

            if (opCode == uWS::OpCode::TEXT) {
                try {
                    auto event = nlohmann::json::parse(message);
                    if (event.value("type", "") == "ping") {
                        nlohmann::json pong = {
                            {"type", "pong"},
                            {"canvas_id", data->canvas_id},
                            {"user_id", data->user_id},
                            {"timestamp", static_cast<long long>(time(nullptr))}
                        };
                        ws->send(pong.dump(), uWS::OpCode::TEXT);
                        return;
                    }

                    if (event.is_object()) {
                        // Socket metadata is authoritative; clients cannot spoof the sender or canvas.
                        event["canvas_id"] = data->canvas_id;
                        event["user_id"] = data->user_id;
                        event["sender_id"] = data->user_id;
                        ws->publish("canvas/" + std::to_string(data->canvas_id), event.dump(), uWS::OpCode::TEXT);
                        return;
                    }
                } catch (...) {
                    // Non-JSON text is still relayed as an opaque payload.
                }
            }

            // uWS WebSocket::publish excludes this sending socket from the topic.
            ws->publish("canvas/" + std::to_string(data->canvas_id), message, opCode);
        },

        .drain = [](auto* /*ws*/) {},

        .close = [this](auto* ws, int code, std::string_view /*message*/) {
            PerSocketData* data = ws->getUserData();
            int canvas_id = data->canvas_id;
            int user_id = data->user_id;
            unregisterSocket(ws);

            std::cout << "[uWebSockets] WebSocket client disconnected: User #" << user_id
                      << " from Canvas #" << canvas_id << " (close code: " << code << ")" << std::endl;

            std::string db_h = pool_.getDbHost();
            int db_p = pool_.getDbPort();
            std::thread([db_h, db_p, canvas_id, user_id]() {
                try {
                    MssqlClient mssql(db_h, db_p);
                    mssql.updateUserSessionDisconnected(user_id);
                } catch (const std::exception& e) {
                    std::cerr << "[uWebSockets] Failed to update DB for user disconnect: " << e.what() << "\n";
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

    std::lock_guard<std::mutex> lock(loop_mutex_);
    listen_socket_ = nullptr;
    loop_ = nullptr;
}
