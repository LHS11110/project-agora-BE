#include "WebSocketServer.hpp"
#include "RedisClient.hpp"
#include <ctime>
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

static bool isLocalProxyPeer(std::string_view ip) {
    return ip == "127.0.0.1" || ip == "::1" || ip == "::ffff:127.0.0.1";
}

static std::string getQueryParam(std::string_view query, const std::string& key) {
    std::string q(query);
    std::string pattern = key + "=";
    auto pos = q.find(pattern);
    if (pos == std::string::npos) return "";
    auto end = q.find('&', pos);
    if (end == std::string::npos) return q.substr(pos + pattern.length());
    return q.substr(pos + pattern.length(), end - (pos + pattern.length()));
}

static std::string jsonPathKey(const nlohmann::json& value) {
    std::string key;
    if (value.is_string()) key = value.get<std::string>();
    else if (value.is_number_integer()) key = std::to_string(value.get<long long>());
    for (std::size_t pos = 0; (pos = key.find('\\', pos)) != std::string::npos; pos += 2) key.insert(pos, 1, '\\');
    for (std::size_t pos = 0; (pos = key.find('"', pos)) != std::string::npos; pos += 2) key.insert(pos, 1, '\\');
    return key;
}

static void persistCanvasEvent(const std::shared_ptr<Canvas>& canvas, const nlohmann::json& event) {
    if (!canvas || !event.is_object()) return;
    const std::string type = event.value("type", "");
    if (type == "chat" || type == "ping" || type == "pong") return;

    RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
    const std::string key = "canvas:" + std::to_string(canvas->getCanvasId());
    if (event.contains("items") && event["items"].is_object()) {
        redis.setJsonPath(key, "$.items", event["items"]);
        return;
    }

    const nlohmann::json* id = nullptr;
    if (event.contains("item_id")) id = &event["item_id"];
    else if (event.contains("item-id")) id = &event["item-id"];
    if (!id) return;

    const std::string item_key = jsonPathKey(*id);
    if (item_key.empty()) return;
    const std::string path = "$[\"items\"][\"" + item_key + "\"]";
    if (type == "item_delete" || type == "delete_item") {
        redis.deleteJsonPath(key, path);
        return;
    }

    if (event.contains("item") && event["item"].is_object()) {
        redis.setJsonPath(key, path, event["item"]);
    } else if (event.contains("data") && event["data"].is_object()) {
        redis.setJsonPath(key, path, event["data"]);
    }
}

static nlohmann::json filterItemsForUser(const nlohmann::json& doc, int user_id) {
    std::unordered_set<std::string> groups;
    bool is_admin = false;
    if (doc.contains("inner-group") && doc["inner-group"].is_object()) {
        for (const auto& [group, members] : doc["inner-group"].items()) {
            if (!members.is_array()) continue;
            for (const auto& member : members) {
                if (member.is_number_integer() && member.get<int>() == user_id) {
                    groups.insert(group);
                    if (group == "admin-group") is_admin = true;
                    break;
                }
            }
        }
    }

    nlohmann::json filtered = nlohmann::json::object();
    if (!doc.contains("items") || !doc["items"].is_object()) return filtered;
    for (const auto& [item_id, item] : doc["items"].items()) {
        bool allowed = is_admin;
        if (!allowed && item.is_object() && item.contains("permission")) {
            const auto& permission = item["permission"];
            if (permission.is_string()) {
                allowed = groups.count(permission.get<std::string>()) > 0;
            } else if (permission.is_array()) {
                for (const auto& group : permission) {
                    if (group.is_string() && groups.count(group.get<std::string>()) > 0) {
                        allowed = true;
                        break;
                    }
                }
            } else if (permission.is_object()) {
                for (const auto& [group, level] : permission.items()) {
                    if (groups.count(group) > 0 && level.is_number_integer() && level.get<int>() > 0) {
                        allowed = true;
                        break;
                    }
                }
            }
        }
        if (allowed) filtered[item_id] = item;
    }
    return filtered;
}

static std::pair<std::unordered_set<std::string>, bool> groupsForUser(const nlohmann::json& doc, int user_id) {
    std::unordered_set<std::string> groups;
    bool is_admin = false;
    if (!doc.contains("inner-group") || !doc["inner-group"].is_object()) return {groups, false};
    for (const auto& [group, members] : doc["inner-group"].items()) {
        if (!members.is_array()) continue;
        for (const auto& member : members) {
            if (member.is_number_integer() && member.get<int>() == user_id) {
                groups.insert(group);
                if (group == "admin-group") is_admin = true;
                break;
            }
        }
    }
    return {std::move(groups), is_admin};
}

static std::unordered_set<std::string> permissionGroups(const nlohmann::json& event) {
    const nlohmann::json* permission = nullptr;
    if (event.contains("permission")) permission = &event["permission"];
    else if (event.contains("item") && event["item"].is_object() && event["item"].contains("permission")) permission = &event["item"]["permission"];
    else if (event.contains("data") && event["data"].is_object() && event["data"].contains("permission")) permission = &event["data"]["permission"];

    std::unordered_set<std::string> groups;
    if (!permission) return groups;
    if (permission->is_string()) groups.insert(permission->get<std::string>());
    else if (permission->is_array()) {
        for (const auto& group : *permission) if (group.is_string()) groups.insert(group.get<std::string>());
    } else if (permission->is_object()) {
        for (const auto& [group, level] : permission->items()) {
            if (level.is_number_integer() && level.get<int>() > 0) groups.insert(group);
        }
    }
    return groups;
}

static bool hasAnyGroup(const PerSocketData* socket, const std::unordered_set<std::string>& required) {
    if (socket->is_admin) return true;
    for (const auto& group : required) if (socket->groups.count(group) > 0) return true;
    return false;
}

static std::string eventItemKey(const nlohmann::json& event) {
    const nlohmann::json* id = nullptr;
    if (event.contains("item_id")) id = &event["item_id"];
    else if (event.contains("item-id")) id = &event["item-id"];
    if (!id) return {};
    if (id->is_string()) return id->get<std::string>();
    if (id->is_number_integer()) return std::to_string(id->get<long long>());
    return {};
}

static nlohmann::json filterItemsForSocket(const nlohmann::json& items, const PerSocketData* socket) {
    nlohmann::json filtered = nlohmann::json::object();
    if (!items.is_object()) return filtered;
    for (const auto& [item_id, item] : items.items()) {
        const auto required = permissionGroups(item);
        if (socket->is_admin || (!required.empty() && hasAnyGroup(socket, required))) {
            filtered[item_id] = item;
        }
    }
    return filtered;
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
    {
        std::unique_lock<std::mutex> lock(worker_mutex_);
        worker_cv_.wait(lock, [this]() { return active_workers_ == 0; });
    }
    pool_.setWebSocketCallbacks({});
}

void WebSocketServer::beginWorker() {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    ++active_workers_;
}

void WebSocketServer::endWorker() {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    if (--active_workers_ == 0) worker_cv_.notify_all();
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
        item_permissions_by_canvas_.erase(data->canvas_id);
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
            .maxBackpressure = 16 * 1024 * 1024,
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
                if (isLocalProxyPeer(client_ip)) {
                    const auto forwarded_ip = req->getHeader("x-real-ip");
                    if (!forwarded_ip.empty()) {
                        client_ip.assign(forwarded_ip.data(), forwarded_ip.size());
                    }
                }
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
                user_id,
                0,
                0,
                false,
                {}
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
            const int canvas_id = data->canvas_id;
            const int user_id = data->user_id;
            beginWorker();
            std::thread([this, ws, canvas_id, user_id]() {
                std::shared_ptr<Canvas> canvas;
                nlohmann::json doc;
                int close_code = 0;
                std::string close_reason;
                bool session_connected = pool_.updateUserSessionConnected(user_id, canvas_id);
                if (!session_connected) {
                    close_code = 1008;
                    close_reason = "Already connected to another canvas";
                } else {
                    canvas = pool_.getOrCreateCanvas(canvas_id);
                    if (!canvas) {
                        close_code = 1011;
                        close_reason = "Canvas initialization failed";
                    } else {
                        RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
                        auto cached_str = redis.get("canvas:" + std::to_string(canvas_id));
                        bool authorized = false;
                        if (cached_str && !cached_str->empty()) {
                            try {
                                doc = nlohmann::json::parse(*cached_str);
                                if (doc.contains("people") && doc["people"].is_array()) {
                                    for (const auto& uid : doc["people"]) {
                                        if (uid.is_number_integer() && uid.get<int>() == user_id) {
                                            authorized = true;
                                            break;
                                        }
                                    }
                                }
                            } catch (...) {
                            }
                        }
                        if (!authorized) {
                            close_code = 1008;
                            close_reason = "Unauthorized access to canvas";
                        }
                    }
                }

                if (close_code != 0 && session_connected) {
                    pool_.updateUserSessionDisconnected(user_id);
                    session_connected = false;
                }

                std::lock_guard<std::mutex> loop_lock(loop_mutex_);
                if (!running_ || !loop_) {
                    if (session_connected) pool_.updateUserSessionDisconnected(user_id);
                    endWorker();
                    return;
                }

                loop_->defer([this, ws, canvas_id, user_id, canvas, doc = std::move(doc), close_code,
                              close_reason = std::move(close_reason), session_connected]() mutable {
                    auto socket_it = sockets_by_canvas_.find(canvas_id);
                    const bool socket_exists = socket_it != sockets_by_canvas_.end() && socket_it->second.count(ws) > 0;
                    if (!socket_exists) {
                        if (session_connected) {
                            const std::string db_host = pool_.getDbHost();
                            const int db_port = pool_.getDbPort();
                            std::thread([db_host, db_port, user_id]() {
                                MssqlClient(db_host, db_port).updateUserSessionDisconnected(user_id);
                            }).detach();
                        }
                        endWorker();
                        return;
                    }
                    if (close_code != 0) {
                        ws->end(close_code, close_reason);
                        endWorker();
                        return;
                    }

                    canvas->connectUser(user_id, 0, 0);
                    auto [groups, is_admin] = groupsForUser(doc, user_id);
                    ws->getUserData()->groups = std::move(groups);
                    ws->getUserData()->is_admin = is_admin;
                    auto& permissions = item_permissions_by_canvas_[canvas_id];
                    permissions.clear();
                    if (doc.contains("items") && doc["items"].is_object()) {
                        for (const auto& [item_id, item] : doc["items"].items()) {
                            permissions[item_id] = permissionGroups(item);
                        }
                    }
                    nlohmann::json init_msg = {
                        {"type", "init_items"}, {"canvas_id", canvas_id}, {"user_id", user_id},
                        {"server_protocol", "uWebSockets"}, {"status", "connected"},
                        {"items", nlohmann::json::object()}
                    };
                    init_msg["items"] = filterItemsForUser(doc, user_id);
                    if (doc.contains("inner-group")) init_msg["inner-group"] = doc["inner-group"];
                    if (doc.contains("canvas-name")) init_msg["canvas_name"] = doc["canvas-name"];
                    ws->send(init_msg.dump(), uWS::OpCode::TEXT);
                    std::cout << "[uWebSockets] Sent init_items to User #" << user_id
                              << " on Canvas #" << canvas_id << std::endl;
                    endWorker();
                });
            }).detach();
        },

        .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
            PerSocketData* data = ws->getUserData();
            
            long long current_time = std::time(nullptr);
            if (current_time != data->last_reset_time) {
                data->last_reset_time = current_time;
                data->message_count = 0;
            }
            data->message_count++;
            
            if (data->message_count > 100) {
                return; // Rate limit exceeded, drop message
            }

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
                        const bool bulk_items = event.contains("items") && event["items"].is_object();
                        const std::string item_key = eventItemKey(event);
                        const bool item_event = bulk_items || !item_key.empty();
                        const bool delete_item = event.value("type", "") == "item_delete"
                            || event.value("type", "") == "delete_item";
                        auto incoming_groups = permissionGroups(event);
                        auto delivery_groups = incoming_groups;

                        bool item_allowed = true;
                        if (bulk_items) {
                            item_allowed = data->is_admin;
                            if (item_allowed) {
                                auto& permissions = item_permissions_by_canvas_[data->canvas_id];
                                permissions.clear();
                                for (const auto& [id, item] : event["items"].items()) {
                                    permissions[id] = permissionGroups(item);
                                }
                            }
                        } else if (!item_key.empty()) {
                            auto& permissions = item_permissions_by_canvas_[data->canvas_id];
                            auto existing = permissions.find(item_key);
                            if (existing != permissions.end()) {
                                delivery_groups = delete_item ? existing->second : incoming_groups;
                                if (!data->is_admin) {
                                    item_allowed = hasAnyGroup(data, existing->second)
                                        && (delete_item || incoming_groups == existing->second);
                                }
                            } else if (!data->is_admin) {
                                item_allowed = !delete_item && !incoming_groups.empty()
                                    && hasAnyGroup(data, incoming_groups);
                            }

                            if (item_allowed) {
                                if (delete_item) permissions.erase(item_key);
                                else permissions[item_key] = incoming_groups;
                            }
                        }

                        if (item_event && !item_allowed) {
                            nlohmann::json denied = {{"type", "error"}, {"code", "ITEM_ACCESS_DENIED"}};
                            ws->send(denied.dump(), uWS::OpCode::TEXT);
                            return;
                        }
                        auto canvas = pool_.getCanvas(data->canvas_id);
                        if (canvas) {
                            std::thread([canvas, persisted_event = event]() {
                                persistCanvasEvent(canvas, persisted_event);
                            }).detach();
                        }
                        const std::string payload = event.dump();
                        auto sockets = sockets_by_canvas_.find(data->canvas_id);
                        if (sockets != sockets_by_canvas_.end()) {
                            for (Socket* target : sockets->second) {
                                if (target == ws) continue;
                                if (bulk_items) {
                                    nlohmann::json filtered_event = event;
                                    filtered_event["items"] = filterItemsForSocket(event["items"], target->getUserData());
                                    target->send(filtered_event.dump(), uWS::OpCode::TEXT);
                                } else if (!item_event || delivery_groups.empty()
                                           || hasAnyGroup(target->getUserData(), delivery_groups)) {
                                    target->send(payload, uWS::OpCode::TEXT);
                                }
                            }
                        }
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

            auto canvas = pool_.getCanvas(canvas_id);
            bool final_connection_closed = false;
            if (canvas) {
                final_connection_closed = canvas->disconnectUser(user_id);
            }

            // If initialization has not completed there is no Canvas yet, so
            // clear any session the background initializer may have reserved.
            if (!canvas || final_connection_closed) {
                std::string db_h = pool_.getDbHost();
                int db_p = pool_.getDbPort();
                std::thread([db_h, db_p, user_id]() {
                    try {
                        MssqlClient mssql(db_h, db_p);
                        mssql.updateUserSessionDisconnected(user_id);
                    } catch (const std::exception& e) {
                        std::cerr << "[uWebSockets] Failed to update DB for user disconnect: " << e.what() << "\n";
                    }
                }).detach();
            }
        }
    };
};

    app.ws<PerSocketData>("/ws/canvas/:canvas_id", createWsHandler());
    app.ws<PerSocketData>("/ws/canvas", createWsHandler());

    // uSockets otherwise enables SO_REUSEPORT, allowing a second process to
    // share this port. A stopped process would then receive part of the
    // WebSocket handshakes and leave clients waiting indefinitely.
    app.listen(host_, ws_port_, LIBUS_LISTEN_EXCLUSIVE_PORT, [this](auto* token) {
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
