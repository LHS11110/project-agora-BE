#include "WebSocketServer.hpp"
#include "RedisClient.hpp"
#include "CanvasPassword.hpp"
#include <ctime>
#include <httplib.h>
#include <iostream>
#include <map>
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

static bool hasCanvasPersistenceTarget(const nlohmann::json& event) {
    if (!event.is_object()) return false;
    const std::string type = event.value("type", "");
    if (type == "chat" || type == "ping" || type == "pong") return false;
    if (event.contains("items") && event["items"].is_object()) return true;
    return event.contains("item_id") || event.contains("item-id");
}

static void drainCanvasPersistenceQueue(const std::shared_ptr<Canvas>& canvas) {
    nlohmann::json event;
    while (canvas && canvas->nextPersistence(event)) {
        try {
            persistCanvasEvent(canvas, event);
        } catch (const std::exception& e) {
            std::cerr << "[uWebSockets] Failed to persist canvas item event: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[uWebSockets] Failed to persist canvas item event\n";
        }
        canvas->endPersistence();
    }
}

static std::unordered_set<std::string> parsePermissionGroups(const nlohmann::json& permission) {
    std::unordered_set<std::string> groups;
    if (permission.is_string()) groups.insert(permission.get<std::string>());
    else if (permission.is_array()) {
        for (const auto& group : permission) if (group.is_string()) groups.insert(group.get<std::string>());
    } else if (permission.is_object()) {
        for (const auto& [group, level] : permission.items()) {
            const bool enabled = level.is_number_unsigned()
                ? level.get<unsigned long long>() > 0
                : level.is_number_integer() && level.get<long long>() > 0;
            if (enabled) groups.insert(group);
        }
    }
    return groups;
}

static std::unordered_set<std::string> permissionGroups(const nlohmann::json& item) {
    if (!item.is_object() || !item.contains("permission")) return {};
    return parsePermissionGroups(item["permission"]);
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
        if (!allowed) {
            const auto required = permissionGroups(item);
            for (const auto& group : required) {
                if (groups.count(group) > 0) {
                    allowed = true;
                    break;
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

static bool normalizeItemEventPermission(nlohmann::json& event, std::unordered_set<std::string>& groups) {
    nlohmann::json* item = nullptr;
    if (event.contains("item") && event["item"].is_object()) item = &event["item"];
    else if (event.contains("data") && event["data"].is_object()) item = &event["data"];
    if (!item) return false;

    if (event.contains("permission")) {
        const auto event_groups = parsePermissionGroups(event["permission"]);
        if (item->contains("permission")) {
            if (event_groups != permissionGroups(*item)) return false;
        } else {
            // Persisted ACLs live on the item payload. Keep accepting legacy
            // top-level ACLs, but make the checked and stored value identical.
            (*item)["permission"] = event["permission"];
        }
    }

    groups = permissionGroups(*item);
    return true;
}

static bool hasAnyGroup(const PerSocketData* socket, const std::unordered_set<std::string>& required) {
    if (socket->is_admin) return true;
    for (const auto& group : required) if (socket->groups.count(group) > 0) return true;
    return false;
}

static bool groupsAreWithinUserGroups(const PerSocketData* socket,
                                      const std::unordered_set<std::string>& required) {
    if (socket->is_admin) return true;
    for (const auto& group : required) {
        if (socket->groups.count(group) == 0) return false;
    }
    return true;
}

static bool isCanvasParticipant(const nlohmann::json& doc, int user_id) {
    if (!doc.contains("people") || !doc["people"].is_array()) return false;
    for (const auto& member : doc["people"]) {
        if (member.is_number_integer() && member.get<int>() == user_id) return true;
    }
    return false;
}

static nlohmann::json canvasSettingsSnapshot(const nlohmann::json& doc, MssqlClient& db) {
    nlohmann::json participants = nlohmann::json::array();
    if (doc.contains("people") && doc["people"].is_array()) {
        for (const auto& member : doc["people"]) {
            if (!member.is_number_integer()) continue;
            auto handle = db.getUserHandle(member.get<int>());
            if (handle) participants.push_back({{"nickname", handle->first}, {"tag_number", handle->second}});
        }
    }
    const bool protected_canvas = doc.contains("canvas-password-hash")
        && doc["canvas-password-hash"].is_string() && !doc["canvas-password-hash"].get<std::string>().empty();
    return {
        {"canvas_id", doc.value("canvas-id", 0)},
        {"canvas_name", doc.value("canvas-name", "")},
        {"description", doc.value("description", "")},
        {"password_protected", protected_canvas},
        {"settings_revision", doc.value("settings-revision", 0LL)},
        {"participants", participants}
    };
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

void WebSocketServer::clearSessionAsync(int user_id, int canvas_id, std::uint64_t session_generation) {
    beginWorker();
    try {
        std::thread([this, user_id, canvas_id, session_generation]() {
            try {
                pool_.updateUserSessionDisconnected(user_id, canvas_id, session_generation);
            } catch (const std::exception& e) {
                std::cerr << "[uWebSockets] Failed to update user disconnect state: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[uWebSockets] Failed to update user disconnect state\n";
            }
            endWorker();
        }).detach();
    } catch (...) {
        endWorker();
        std::cerr << "[uWebSockets] Failed to schedule user session cleanup for User #" << user_id << "\n";
    }
}

void WebSocketServer::refreshUserSessionGeneration(int canvas_id, int user_id,
                                                   std::uint64_t session_generation) {
    auto it = sockets_by_canvas_.find(canvas_id);
    if (it == sockets_by_canvas_.end()) return;
    for (Socket* ws : it->second) {
        auto* data = ws->getUserData();
        if (data->user_id == user_id && data->access_authorized) {
            data->session_generation = session_generation;
        }
    }
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
            if (!data->access_authorized) continue;
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
            if (ws->getUserData()->access_authorized && ws->getUserData()->user_id == user_id) {
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

void WebSocketServer::handleCanvasSettings(Socket* ws, const nlohmann::json& event) {
    const PerSocketData* identity = ws->getUserData();
    const std::string request_id = event.contains("request_id") && event["request_id"].is_string()
        ? event["request_id"].get<std::string>().substr(0, 64) : "";
    auto reject = [&](const char* code) {
        ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                {"request_id", request_id}, {"code", code}}.dump(), uWS::OpCode::TEXT);
    };
    auto canvas = pool_.getCanvas(identity->canvas_id);
    if (!canvas) return reject("CANVAS_NOT_READY");
    std::lock_guard<std::mutex> settings_lock(canvas->settings_mutex);
    if (canvas->unloading) return reject("CANVAS_NOT_READY");

    RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
    const std::string key = "canvas:" + std::to_string(identity->canvas_id);
    auto raw = redis.get(key);
    if (!raw) return reject("SETTINGS_STORAGE_ERROR");
    nlohmann::json doc;
    try { doc = nlohmann::json::parse(*raw); } catch (...) { return reject("SETTINGS_STORAGE_ERROR"); }
    if (!doc.is_object()) return reject("SETTINGS_STORAGE_ERROR");

    MssqlClient db(pool_.getDbHost(), pool_.getDbPort());
    if (!db.isCanvasAssignedToServer(identity->canvas_id, pool_.getCppServerIp(), pool_.getCppServerPort())) {
        return reject("CANVAS_NOT_READY");
    }
    if (db.getActiveUserId(identity->nickname, identity->tag_number) != identity->user_id
        || !isCanvasParticipant(doc, identity->user_id)) return reject("SETTINGS_ACCESS_DENIED");

    const std::string type = event["type"].get<std::string>();
    if (type == "canvas_settings_get") {
        ws->send(nlohmann::json{{"type", "canvas_settings_snapshot"},
                                {"settings", canvasSettingsSnapshot(doc, db)}}.dump(), uWS::OpCode::TEXT);
        return;
    }

    if (type != "canvas_settings_update") return reject("SETTINGS_INVALID_INPUT");

    if (!groupsForUser(doc, identity->user_id).second) return reject("SETTINGS_ACCESS_DENIED");
    if (!event.contains("expected_revision") || !event["expected_revision"].is_number_integer()) {
        return reject("SETTINGS_REVISION_REQUIRED");
    }
    const long long revision = doc.value("settings-revision", 0LL);
    if (event["expected_revision"].get<long long>() != revision) {
        ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                {"request_id", request_id}, {"code", "SETTINGS_CONFLICT"},
                                {"settings", canvasSettingsSnapshot(doc, db)}}.dump(), uWS::OpCode::TEXT);
        return;
    }
    if (!event.contains("field") || !event["field"].is_string()) return reject("SETTINGS_INVALID_INPUT");
    const std::string field = event["field"].get<std::string>();
    std::vector<std::pair<std::string, nlohmann::json>> changes;
    std::map<std::string, nlohmann::json> es_changes;
    std::map<std::string, std::string> redis_paths;
    std::map<std::string, nlohmann::json> original_settings;
    auto rememberOriginal = [&](const std::string& document_field) {
        if (doc.contains(document_field)) original_settings.emplace(document_field, doc[document_field]);
    };
    auto addChange = [&](const std::string& redis_path, const std::string& document_field,
                         const nlohmann::json& value) {
        changes.emplace_back(redis_path, value);
        es_changes[document_field] = value;
        redis_paths[document_field] = redis_path;
    };
    int removed_user_id = 0;
    if (field == "name" || field == "description" || field == "password") {
        if (!event.contains("value") || !event["value"].is_string()) return reject("SETTINGS_INVALID_INPUT");
        const std::string value = event["value"].get<std::string>();
        if ((field == "name" && value.empty()) || value.size() > (field == "description" ? 4000U : 256U)) {
            return reject("SETTINGS_INVALID_INPUT");
        }
        if (field == "name") {
            rememberOriginal("canvas-name");
            doc["canvas-name"] = value;
            addChange("$[\"canvas-name\"]", "canvas-name", value);
        } else if (field == "description") {
            rememberOriginal("description");
            doc["description"] = value;
            addChange("$.description", "description", value);
        } else {
            rememberOriginal("canvas-password-hash");
            nlohmann::json hash = nullptr;
            if (value.find_first_not_of(" \t\n\r\f\v") != std::string::npos) {
                auto generated = hashCanvasPassword(value);
                if (!generated) return reject("SETTINGS_STORAGE_ERROR");
                hash = *generated;
            }
            doc["canvas-password-hash"] = hash;
            addChange("$[\"canvas-password-hash\"]", "canvas-password-hash", hash);
        }
    } else if (field == "participant_add" || field == "participant_remove") {
        if (!event.contains("nickname") || !event["nickname"].is_string()
            || !event.contains("tag_number") || !event["tag_number"].is_number_integer()) {
            return reject("SETTINGS_INVALID_INPUT");
        }
        const std::string nickname = event["nickname"].get<std::string>();
        const int tag = event["tag_number"].get<int>();
        if (nickname.empty() || nickname.size() > 200 || tag < 1) return reject("SETTINGS_INVALID_INPUT");
        if (!doc.contains("people") || !doc["people"].is_array()) return reject("SETTINGS_STORAGE_ERROR");
        if (!doc.contains("inner-group") || !doc["inner-group"].is_object()) return reject("SETTINGS_STORAGE_ERROR");
        rememberOriginal("people");
        rememberOriginal("inner-group");
        int target_id = -1;
        if (field == "participant_add") {
            target_id = db.getActiveUserId(nickname, tag);
            if (target_id <= 0) return reject("SETTINGS_USER_NOT_FOUND");
            if (isCanvasParticipant(doc, target_id)) return reject("SETTINGS_ALREADY_PARTICIPANT");
            doc["people"].push_back(target_id);
            const std::string group = doc.contains("init-group") && doc["init-group"].is_string()
                ? doc["init-group"].get<std::string>() : "default";
            if (!doc["inner-group"].contains(group) || !doc["inner-group"][group].is_array()) {
                doc["inner-group"][group] = nlohmann::json::array();
            }
            doc["inner-group"][group].push_back(target_id);
        } else {
            for (const auto& member : doc["people"]) {
                if (!member.is_number_integer()) continue;
                auto handle = db.getUserHandle(member.get<int>());
                if (handle && handle->first == nickname && handle->second == tag) {
                    target_id = member.get<int>();
                    break;
                }
            }
            if (target_id <= 0) return reject("SETTINGS_USER_NOT_FOUND");
            if (doc.contains("admin-user-id") && doc["admin-user-id"].is_number_integer()
                && doc["admin-user-id"].get<int>() == target_id) return reject("SETTINGS_OWNER_REQUIRED");
            nlohmann::json remaining = nlohmann::json::array();
            for (const auto& member : doc["people"]) {
                if (!member.is_number_integer() || member.get<int>() != target_id) remaining.push_back(member);
            }
            doc["people"] = std::move(remaining);
            for (auto& [group, members] : doc["inner-group"].items()) {
                if (!members.is_array()) continue;
                nlohmann::json updated = nlohmann::json::array();
                for (const auto& member : members) {
                    if (!member.is_number_integer() || member.get<int>() != target_id) updated.push_back(member);
                }
                members = std::move(updated);
            }
            removed_user_id = target_id;
        }
        addChange("$.people", "people", doc["people"]);
        addChange("$[\"inner-group\"]", "inner-group", doc["inner-group"]);
    } else {
        return reject("SETTINGS_INVALID_INPUT");
    }

    doc["settings-revision"] = revision + 1;
    addChange("$[\"settings-revision\"]", "settings-revision", revision + 1);
    const auto stored = redis.compareAndSetJsonPaths(key, revision, changes);
    if (stored == RedisClient::CompareSetResult::Conflict) {
        auto latest_raw = redis.get(key);
        nlohmann::json latest = doc;
        if (latest_raw) {
            try { latest = nlohmann::json::parse(*latest_raw); } catch (...) {}
        }
        ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                {"request_id", request_id}, {"code", "SETTINGS_CONFLICT"},
                                {"settings", canvasSettingsSnapshot(latest, db)}}.dump(), uWS::OpCode::TEXT);
        return;
    }
    if (stored != RedisClient::CompareSetResult::Applied) return reject("SETTINGS_STORAGE_ERROR");
    EsClient es(pool_.getEsHost(), pool_.getEsPort());
    if (!es.patchCanvasFields(identity->canvas_id, es_changes)) {
        std::vector<std::pair<std::string, nlohmann::json>> rollback;
        std::map<std::string, nlohmann::json> restore_in_es;
        for (const auto& [document_field, updated_value] : es_changes) {
            (void)updated_value;
            const nlohmann::json original_value = document_field == "settings-revision"
                ? nlohmann::json(revision)
                : (original_settings.contains(document_field)
                    ? original_settings.at(document_field) : nlohmann::json(nullptr));
            rollback.emplace_back(redis_paths.at(document_field), original_value);
            restore_in_es[document_field] = original_value;
        }
        if (redis.compareAndSetJsonPaths(key, revision + 1, rollback) != RedisClient::CompareSetResult::Applied) {
            std::cerr << "[uWebSockets] Could not roll back Redis canvas settings after Elasticsearch failure for Canvas #"
                      << identity->canvas_id << "\n";
        }
        if (!es.patchCanvasFields(identity->canvas_id, restore_in_es)) {
            std::cerr << "[uWebSockets] Could not compensate Elasticsearch settings after a failed canvas update for Canvas #"
                      << identity->canvas_id << "\n";
        }
        return reject("SETTINGS_STORAGE_ERROR");
    }
    if (field == "name") canvas->setCanvasName(doc["canvas-name"].get<std::string>());

    const auto snapshot = canvasSettingsSnapshot(doc, db);
    ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", true},
                            {"request_id", request_id}, {"settings", snapshot}}.dump(), uWS::OpCode::TEXT);
    auto sockets = sockets_by_canvas_.find(identity->canvas_id);
    if (sockets != sockets_by_canvas_.end()) {
        const std::string notification = nlohmann::json{{"type", "canvas_settings_changed"},
                                                        {"settings", snapshot}}.dump();
        for (Socket* target : sockets->second) {
            if (target != ws && target->getUserData()->access_authorized
                && isCanvasParticipant(doc, target->getUserData()->user_id)) {
                target->send(notification, uWS::OpCode::TEXT);
            }
        }
    }
    if (removed_user_id > 0) pool_.disconnectUser(identity->canvas_id, removed_user_id);
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

            std::optional<AuthenticatedUser> authenticated_user;
            std::string token = getQueryParam(query, "token");
            if (token_validator_ && !token.empty()) {
                std::string client_ip = std::string(res->getRemoteAddressAsText());
                if (isLocalProxyPeer(client_ip)) {
                    const auto forwarded_ip = req->getHeader("x-real-ip");
                    if (!forwarded_ip.empty()) {
                        client_ip.assign(forwarded_ip.data(), forwarded_ip.size());
                    }
                }
                authenticated_user = token_validator_(token, canvas_id, client_ip);
            }

            if (!authenticated_user || authenticated_user->user_id <= 0 || authenticated_user->tag_number < 0) {
                std::cout << "[uWebSockets] Upgrade rejected: 401 Unauthorized (Invalid, missing, or unauthorized JWT token for canvas #"
                          << canvas_id << ")" << std::endl;
                res->writeStatus("401 Unauthorized")->end("Invalid, missing, or unauthorized JWT token");
                return;
            }

            res->template upgrade<PerSocketData>({
                canvas_id,
                authenticated_user->user_id,
                authenticated_user->tag_number,
                std::move(authenticated_user->nickname),
                authenticated_user->settings_revision,
                0,
                0,
                0,
                false,
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
            const int canvas_id = data->canvas_id;
            const int user_id = data->user_id;
            const long long token_revision = data->settings_revision;
            beginWorker();
            std::thread([this, ws, canvas_id, user_id, token_revision]() {
                std::shared_ptr<Canvas> canvas;
                nlohmann::json doc;
                int close_code = 0;
                std::string close_reason;
                // Check participation against Redis/Elasticsearch before a
                // session reservation or CanvasPool cache allocation occurs.
                bool session_connected = false;
                std::uint64_t session_generation = 0;
                if (!pool_.isCanvasAccessAuthorized(canvas_id, user_id, token_revision)) {
                    close_code = 1008;
                    close_reason = "Canvas access is unauthorized or settings changed";
                } else {
                    canvas = pool_.getOrCreateCanvas(canvas_id);
                    if (!canvas) {
                        close_code = 1011;
                        close_reason = "Canvas initialization failed";
                    } else {
                        const auto reservation = pool_.updateUserSessionConnected(user_id, canvas_id);
                        session_connected = reservation.has_value();
                        if (!session_connected) {
                            close_code = 1008;
                            close_reason = "Already connected to another canvas or the canvas assignment changed";
                        } else {
                            session_generation = *reservation;
                        }
                    }
                }

                std::lock_guard<std::mutex> loop_lock(loop_mutex_);
                if (!running_ || !loop_) {
                    if (session_connected && (!canvas || !canvas->isUserActive(user_id))) {
                        pool_.updateUserSessionDisconnected(user_id, canvas_id, session_generation);
                    }
                    endWorker();
                    return;
                }

                loop_->defer([this, ws, canvas_id, user_id, token_revision, session_generation,
                              canvas, doc = std::move(doc), close_code,
                              close_reason = std::move(close_reason), session_connected]() mutable {
                    auto socket_it = sockets_by_canvas_.find(canvas_id);
                    const bool socket_exists = socket_it != sockets_by_canvas_.end() && socket_it->second.count(ws) > 0;
                    if (!socket_exists) {
                        if (session_connected) {
                            if (canvas && canvas->isUserActive(user_id)) {
                                refreshUserSessionGeneration(canvas_id, user_id, session_generation);
                            } else {
                                clearSessionAsync(user_id, canvas_id, session_generation);
                            }
                        }
                        endWorker();
                        return;
                    }
                    if (close_code != 0) {
                        if (session_connected) {
                            if (canvas && canvas->isUserActive(user_id)) {
                                refreshUserSessionGeneration(canvas_id, user_id, session_generation);
                            } else {
                                clearSessionAsync(user_id, canvas_id, session_generation);
                            }
                        }
                        ws->end(close_code, close_reason);
                        endWorker();
                        return;
                    }
                    bool settings_unchanged = false;
                    {
                        std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);
                        if (!canvas->unloading) {
                            // A queued item mutation may already have been
                            // broadcast while its Redis write is still pending.
                            // Drain those writes before this socket's snapshot.
                            canvas->waitForPersistenceIdle(settings_lock);
                            if (!canvas->unloading) {
                                RedisClient latest_redis(canvas->getRedisIp(), canvas->getRedisPort());
                                auto latest_raw = latest_redis.get("canvas:" + std::to_string(canvas_id));
                                if (latest_raw) {
                                    try {
                                        auto latest_doc = nlohmann::json::parse(*latest_raw);
                                        settings_unchanged = latest_doc.is_object()
                                            && latest_doc.value("settings-revision", 0LL) == token_revision;
                                        if (settings_unchanged) {
                                            doc = std::move(latest_doc);
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
                                            ws->getUserData()->access_authorized = true;
                                        }
                                    } catch (...) {}
                                }
                            }
                        }
                    }
                    if (!settings_unchanged) {
                        if (canvas->isUserActive(user_id)) {
                            refreshUserSessionGeneration(canvas_id, user_id, session_generation);
                        } else {
                            clearSessionAsync(user_id, canvas_id, session_generation);
                        }
                        ws->end(1008, "Canvas settings changed; request a new access token");
                        endWorker();
                        return;
                    }

                    ws->getUserData()->session_generation = session_generation;
                    refreshUserSessionGeneration(canvas_id, user_id, session_generation);
                    ws->subscribe("canvas/" + std::to_string(canvas_id));
                    nlohmann::json init_msg = {
                        {"type", "init_items"}, {"canvas_id", canvas_id},
                        {"server_protocol", "uWebSockets"}, {"status", "connected"},
                        {"items", nlohmann::json::object()}
                    };
                    init_msg["items"] = filterItemsForUser(doc, user_id);
                    nlohmann::json visible_groups = nlohmann::json::array();
                    for (const auto& group : ws->getUserData()->groups) visible_groups.push_back(group);
                    init_msg["groups"] = std::move(visible_groups);
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
            if (!data->access_authorized) {
                ws->end(1008, "Canvas access is not authorized");
                return;
            }
            
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
                bool parsed_json = false;
                try {
                    auto event = nlohmann::json::parse(message);
                    parsed_json = true;
                    if (event.value("type", "") == "ping") {
                        nlohmann::json pong = {
                            {"type", "pong"},
                            {"canvas_id", data->canvas_id},
                            {"timestamp", static_cast<long long>(time(nullptr))}
                        };
                        ws->send(pong.dump(), uWS::OpCode::TEXT);
                        return;
                    }

                    if (event.is_object() && event.contains("type") && event["type"].is_string()
                        && event["type"].get<std::string>().rfind("canvas_settings_", 0) == 0) {
                        try {
                            handleCanvasSettings(ws, event);
                        } catch (const std::exception& e) {
                            std::cerr << "[uWebSockets] Canvas settings event rejected: " << e.what() << "\n";
                            ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                                    {"code", "SETTINGS_INVALID_INPUT"}}.dump(), uWS::OpCode::TEXT);
                        }
                        return;
                    }

                    if (event.is_object()) {
                        // Socket metadata is authoritative; clients cannot spoof identities or canvas.
                        event["canvas_id"] = data->canvas_id;
                        // Internal database identities never cross the WebSocket boundary.
                        event.erase("user_id");
                        event.erase("userId");
                        event.erase("sender_id");
                        event.erase("senderId");
                        if (event.value("type", "") == "chat") {
                            // Public chat payloads use the nickname/tag pair,
                            // while internal authorization keeps the DB user ID.
                            event.erase("tagNumber");
                            event["sender"] = data->nickname;
                            event["tag_number"] = data->tag_number;
                        } else {
                            // Keep database identity server-side. Public events use
                            // canvas/item fields only and never expose user IDs.
                        }
                        const bool bulk_items = event.contains("items") && event["items"].is_object();
                        const std::string item_key = eventItemKey(event);
                        const std::string event_type = event.value("type", "");
                        const bool delete_item = event_type == "item_delete" || event_type == "delete_item";
                        const bool item_mutation = event_type.rfind("item_", 0) == 0
                            || event_type == "delete_item" || event.contains("item")
                            || event.contains("data") || event.contains("permission")
                            || event.contains("items");
                        const bool malformed_item_event = !bulk_items && item_key.empty() && item_mutation;
                        const bool item_event = bulk_items || !item_key.empty() || malformed_item_event;
                        std::unordered_set<std::string> incoming_groups;
                        const bool permission_payload_valid = !malformed_item_event
                            && (bulk_items || delete_item || item_key.empty()
                                || normalizeItemEventPermission(event, incoming_groups));
                        auto delivery_groups = incoming_groups;
                        std::unordered_set<std::string> previous_groups;
                        bool had_previous_item = false;
                        std::unordered_map<std::string, std::unordered_set<std::string>> previous_canvas_permissions;
                        auto restoreItemPermissions = [&]() {
                            auto& permissions = item_permissions_by_canvas_[data->canvas_id];
                            if (bulk_items) {
                                permissions = previous_canvas_permissions;
                            } else if (!item_key.empty()) {
                                if (had_previous_item) permissions[item_key] = previous_groups;
                                else permissions.erase(item_key);
                            }
                        };

                        bool item_allowed = permission_payload_valid;
                        if (bulk_items) {
                            item_allowed = data->is_admin;
                            if (item_allowed) {
                                auto& permissions = item_permissions_by_canvas_[data->canvas_id];
                                previous_canvas_permissions = permissions;
                                permissions.clear();
                                for (const auto& [id, item] : event["items"].items()) {
                                    permissions[id] = permissionGroups(item);
                                }
                            }
                        } else if (!item_key.empty()) {
                            auto& permissions = item_permissions_by_canvas_[data->canvas_id];
                            auto existing = permissions.find(item_key);
                            if (existing != permissions.end()) {
                                had_previous_item = true;
                                previous_groups = existing->second;
                                delivery_groups = delete_item ? existing->second : incoming_groups;
                                if (!data->is_admin) {
                                    item_allowed = item_allowed && hasAnyGroup(data, existing->second)
                                        && (delete_item || incoming_groups == existing->second);
                                }
                            } else if (!data->is_admin) {
                                item_allowed = !delete_item && !incoming_groups.empty()
                                    && hasAnyGroup(data, incoming_groups)
                                    && groupsAreWithinUserGroups(data, incoming_groups);
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
                        if (hasCanvasPersistenceTarget(event)) {
                            auto canvas = pool_.getCanvas(data->canvas_id);
                            bool start_worker = false;
                            if (!canvas || !canvas->enqueuePersistence(event, start_worker)) {
                                restoreItemPermissions();
                                ws->end(1012, "Canvas is closing");
                                return;
                            }
                            if (start_worker) {
                                try {
                                    std::thread([canvas]() { drainCanvasPersistenceQueue(canvas); }).detach();
                                } catch (...) {
                                    canvas->cancelPersistenceQueue();
                                    restoreItemPermissions();
                                    ws->end(1012, "Canvas persistence is unavailable");
                                    return;
                                }
                            }
                        }
                        const std::string payload = event.dump();
                        auto sockets = sockets_by_canvas_.find(data->canvas_id);
                        if (sockets != sockets_by_canvas_.end()) {
                            for (Socket* target : sockets->second) {
                                if (target == ws || !target->getUserData()->access_authorized) continue;
                                if (bulk_items) {
                                    nlohmann::json filtered_event = event;
                                    filtered_event["items"] = filterItemsForSocket(event["items"], target->getUserData());
                                    target->send(filtered_event.dump(), uWS::OpCode::TEXT);
                                    if (!target->getUserData()->is_admin) {
                                        for (const auto& [item_id, old_groups] : previous_canvas_permissions) {
                                            if (!hasAnyGroup(target->getUserData(), old_groups)) continue;
                                            const auto updated_item = event["items"].find(item_id);
                                            const bool remains_visible = updated_item != event["items"].end()
                                                && hasAnyGroup(target->getUserData(), permissionGroups(*updated_item));
                                            if (!remains_visible) {
                                                nlohmann::json revoked = {
                                                    {"type", "item_delete"},
                                                    {"canvas_id", data->canvas_id},
                                                    {"item_id", item_id}
                                                };
                                                target->send(revoked.dump(), uWS::OpCode::TEXT);
                                            }
                                        }
                                    }
                                } else if (!item_event) {
                                    target->send(payload, uWS::OpCode::TEXT);
                                } else if (delivery_groups.empty()
                                               ? target->getUserData()->is_admin
                                               : hasAnyGroup(target->getUserData(), delivery_groups)) {
                                    target->send(payload, uWS::OpCode::TEXT);
                                } else if (!delete_item && had_previous_item
                                           && hasAnyGroup(target->getUserData(), previous_groups)) {
                                    // Remove a revoked item from connected clients' local state.
                                    // New access is still checked from the persisted ACL on every join.
                                    nlohmann::json revoked = {
                                        {"type", "item_delete"},
                                        {"canvas_id", data->canvas_id}
                                    };
                                    if (event.contains("item_id")) revoked["item_id"] = event["item_id"];
                                    else if (event.contains("item-id")) revoked["item-id"] = event["item-id"];
                                    target->send(revoked.dump(), uWS::OpCode::TEXT);
                                }
                            }
                        }
                        return;
                    }
                } catch (...) {
                    if (parsed_json) {
                        ws->send(nlohmann::json{{"type", "error"}, {"code", "INVALID_EVENT"}}.dump(),
                                 uWS::OpCode::TEXT);
                        return;
                    }
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
            const bool access_authorized = data->access_authorized;
            const std::uint64_t session_generation = data->session_generation;
            unregisterSocket(ws);

            std::cout << "[uWebSockets] WebSocket client disconnected: User #" << user_id
                      << " from Canvas #" << canvas_id << " (close code: " << code << ")" << std::endl;

            auto canvas = pool_.getCanvas(canvas_id);
            bool final_connection_closed = false;
            if (canvas && access_authorized) {
                final_connection_closed = canvas->disconnectUser(user_id);
            }

            // The background initializer clears reservations for rejected or
            // prematurely closed sockets. Only established sockets own a Canvas entry.
            if (access_authorized && (!canvas || final_connection_closed)) {
                clearSessionAsync(user_id, canvas_id, session_generation);
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
