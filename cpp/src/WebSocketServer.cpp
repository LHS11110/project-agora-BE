#include "WebSocketServer.hpp"
#include "RedisClient.hpp"
#include "CanvasPassword.hpp"
#include <ctime>
#include <chrono>
#include <algorithm>
#include <httplib.h>
#include <iostream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <pthread.h>
#include <random>
#include <sstream>
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

static std::string createRtcPeerId() {
    static std::atomic_uint64_t sequence{0};
    std::uint64_t random_value = 0;
    try {
        std::random_device source;
        random_value = (static_cast<std::uint64_t>(source()) << 32) ^ source();
    } catch (...) {
        random_value = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    std::ostringstream id;
    id << std::hex << random_value << '-' << sequence.fetch_add(1, std::memory_order_relaxed);
    return id.str();
}

static std::uint64_t createSocketConnectionId() {
    static std::atomic_uint64_t sequence{1};
    std::uint64_t id = sequence.fetch_add(1, std::memory_order_relaxed);
    if (id == 0) id = sequence.fetch_add(1, std::memory_order_relaxed);
    return id;
}

static std::string jsonPathKey(const nlohmann::json& value) {
    std::string key;
    if (value.is_string()) key = value.get<std::string>();
    else if (value.is_number_integer()) key = std::to_string(value.get<long long>());
    for (std::size_t pos = 0; (pos = key.find('\\', pos)) != std::string::npos; pos += 2) key.insert(pos, 1, '\\');
    for (std::size_t pos = 0; (pos = key.find('"', pos)) != std::string::npos; pos += 2) key.insert(pos, 1, '\\');
    return key;
}

static bool isChatRoomItem(const nlohmann::json& item);
static std::uint64_t positiveSequence(const nlohmann::json& value);
static std::uint64_t chatRoomNextSequence(const nlohmann::json& item);

static nlohmann::json storedChatMessage(const nlohmann::json& event) {
    nlohmann::json stored = {
        {"sequence", event.value("sequence", 0ULL)},
        {"text", event.value("text", "")},
        {"sender", event.value("sender", "")},
        {"sender_user_id", event.value("sender_user_id", 0)},
        {"tag_number", event.value("tag_number", 0)},
        {"created_at", event.value("created_at", 0LL)}
    };
    if (event.contains("request_id") && event["request_id"].is_string()) {
        stored["request_id"] = event["request_id"];
    }
    return stored;
}

static bool persistCanvasEvent(const std::shared_ptr<Canvas>& canvas, const nlohmann::json& event) {
    if (!canvas || !event.is_object()) return false;
    const std::string type = event.value("type", "");
    if (type == "ping" || type == "pong" || type == "item_crdt_change") return true;

    RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
    const std::string key = "canvas:" + std::to_string(canvas->getCanvasId());
    if (type == "chat") {
        if (!event.contains("room_id") || !event["room_id"].is_string()
            || !event.contains("sequence") || !event["sequence"].is_number_unsigned()) return false;
        const std::string room_id = event["room_id"].get<std::string>();
        const std::string path_key = jsonPathKey(room_id);
        if (path_key.empty()) return false;
        const std::string item_path = "$[\"items\"][\"" + path_key + "\"]";
        const auto stored_message = storedChatMessage(event);
        if (event.value("room_created", false)) {
            nlohmann::json room = {
                {"type", "chat_room"},
                {"permission", event.value("room_permission", nlohmann::json::array())},
                {"data", nlohmann::json::array({stored_message})},
                {"next_sequence", event["sequence"].get<std::uint64_t>() + 1}
            };
            if (!redis.setJsonPath(key, item_path, room)) {
                std::cerr << "[uWebSockets] Failed to create chat room item '" << room_id << "'\n";
                return false;
            }
        } else if (!redis.appendChatMessage(key, room_id, event["sequence"].get<std::uint64_t>(), stored_message)) {
            std::cerr << "[uWebSockets] Failed to append chat message to room '" << room_id << "'\n";
            return false;
        }
        return true;
    }
    if (event.contains("items") && event["items"].is_object()) {
        nlohmann::json items = event["items"];
        for (auto& [item_id, item] : items.items()) {
            if (!isChatRoomItem(item) || item.contains("data")) continue;
            const nlohmann::json item_id_json = item_id;
            const std::string path_key = jsonPathKey(item_id_json);
            const std::string item_path = "$[\"items\"][\"" + path_key + "\"]";
            auto old_data = redis.getJsonPath(key, item_path + "[\"data\"]");
            if (old_data) {
                try {
                    const auto result = nlohmann::json::parse(*old_data);
                    if (result.is_array() && !result.empty()) item["data"] = result[0];
                } catch (...) {}
            }
            if (!item.contains("data")) item["data"] = nlohmann::json::array();
            std::uint64_t next_sequence = chatRoomNextSequence(item);
            auto old_next = redis.getJsonPath(key, item_path + "[\"next_sequence\"]");
            if (old_next) {
                try {
                    const auto result = nlohmann::json::parse(*old_next);
                    if (result.is_array() && !result.empty()) {
                        next_sequence = std::max(next_sequence, positiveSequence(result[0]));
                    }
                } catch (...) {}
            }
            item["next_sequence"] = next_sequence;
        }
        return redis.setJsonPath(key, "$.items", items);
    }

    const nlohmann::json* id = nullptr;
    if (event.contains("item_id")) id = &event["item_id"];
    else if (event.contains("item-id")) id = &event["item-id"];
    if (!id) return false;

    const std::string item_key = jsonPathKey(*id);
    if (item_key.empty()) return false;
    const std::string path = "$[\"items\"][\"" + item_key + "\"]";
    if (type == "item_delete" || type == "delete_item") {
        return redis.deleteJsonPath(key, path);
    }

    const nlohmann::json* item_payload = nullptr;
    if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
    else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
    if (item_payload) {
        nlohmann::json item = *item_payload;
        const std::string item_kind = item.value("kind", "");
        if (item_kind == "text" || item_kind == "note" || item_kind == "code") {
            const auto stored_item = redis.getJsonPath(key, path);
            if (stored_item) {
                try {
                    const auto matches = nlohmann::json::parse(*stored_item);
                    if (matches.is_array() && !matches.empty() && matches[0].is_object()) {
                        const auto& previous_item = matches[0];
                        if (previous_item.value("kind", "") == item_kind) {
                            // Keep the base snapshot immutable and union changes supplied at save
                            // time. This allows concurrent offline branches to merge after reconnect.
                            if (previous_item.contains("automerge_snapshot")
                                && previous_item["automerge_snapshot"].is_string()) {
                                item["automerge_snapshot"] = previous_item["automerge_snapshot"];
                            }
                            nlohmann::json merged_changes = nlohmann::json::array();
                            std::unordered_set<std::string> seen_changes;
                            auto append_changes = [&merged_changes, &seen_changes](const nlohmann::json& changes) {
                                if (!changes.is_array()) return;
                                for (const auto& change : changes) {
                                    if (!change.is_object() || !change.contains("change")
                                        || !change["change"].is_string()) continue;
                                    const std::string encoded = change["change"].get<std::string>();
                                    if (seen_changes.insert(encoded).second) merged_changes.push_back(change);
                                }
                            };
                            if (previous_item.contains("automerge_changes")) {
                                append_changes(previous_item["automerge_changes"]);
                            }
                            if (item.contains("automerge_changes")) append_changes(item["automerge_changes"]);
                            item["automerge_changes"] = std::move(merged_changes);
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[uWebSockets] Failed to preserve Automerge history for item '"
                              << item_key << "': " << e.what() << "\n";
                }
            }
        }
        if (event.value("_preserve_chat_history", false) && isChatRoomItem(item)) {
            auto old_data = redis.getJsonPath(key, path + "[\"data\"]");
            if (old_data) {
                try {
                    const auto result = nlohmann::json::parse(*old_data);
                    if (result.is_array() && !result.empty()) item["data"] = result[0];
                } catch (...) {}
            }
            if (item.contains("data") && item["data"].is_array()) {
                std::uint64_t next_sequence = chatRoomNextSequence(item);
                auto old_next = redis.getJsonPath(key, path + "[\"next_sequence\"]");
                if (old_next) {
                    try {
                        const auto result = nlohmann::json::parse(*old_next);
                        if (result.is_array() && !result.empty()) {
                            next_sequence = std::max(next_sequence, positiveSequence(result[0]));
                        }
                    } catch (...) {}
                }
                item["next_sequence"] = next_sequence;
            }
        }
        return redis.setJsonPath(key, path, item);
    }
    return false;
}

static bool hasCanvasPersistenceTarget(const nlohmann::json& event) {
    if (!event.is_object()) return false;
    const std::string type = event.value("type", "");
    if (type == "ping" || type == "pong") return false;
    if (type == "chat") return event.contains("room_id") && event["room_id"].is_string();
    if (event.contains("items") && event["items"].is_object()) return true;
    return event.contains("item_id") || event.contains("item-id");
}

static void drainCanvasPersistenceQueue(const std::shared_ptr<Canvas>& canvas) {
    nlohmann::json event;
    std::uint64_t ticket = 0;
    while (canvas && canvas->nextPersistence(event, ticket)) {
        bool succeeded = false;
        try {
            succeeded = persistCanvasEvent(canvas, event);
        } catch (const std::exception& e) {
            std::cerr << "[uWebSockets] Failed to persist canvas item event: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[uWebSockets] Failed to persist canvas item event\n";
        }
        if (!succeeded) {
            std::cerr << "[uWebSockets] Canvas #" << canvas->getCanvasId()
                      << " Redis write failed; preventing Elasticsearch snapshot\n";
        }
        canvas->endPersistence(ticket, succeeded);
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

static bool isChatRoomItem(const nlohmann::json& item) {
    return item.is_object() && item.contains("type") && item["type"].is_string()
        && item["type"].get<std::string>() == "chat_room";
}

static std::uint64_t positiveSequence(const nlohmann::json& value) {
    try {
        if (value.is_number_unsigned()) return value.get<std::uint64_t>();
        if (value.is_number_integer()) {
            const auto sequence = value.get<long long>();
            return sequence > 0 ? static_cast<std::uint64_t>(sequence) : 0;
        }
    } catch (...) {}
    return 0;
}

static std::uint64_t chatRoomNextSequence(const nlohmann::json& item) {
    std::uint64_t next = item.is_object() && item.contains("next_sequence")
        ? positiveSequence(item["next_sequence"]) : 1;
    if (next == 0) next = 1;
    if (item.is_object() && item.contains("data") && item["data"].is_array()) {
        for (const auto& message : item["data"]) {
            if (message.is_object() && message.contains("sequence")) {
                const auto sequence = positiveSequence(message["sequence"]);
                if (sequence < std::numeric_limits<std::uint64_t>::max() && sequence + 1 > next) next = sequence + 1;
            }
        }
    }
    return next;
}

static void normalizeChatRoomItem(nlohmann::json& item) {
    if (!isChatRoomItem(item)) return;
    if (item.contains("data") && !item["data"].is_array()) item["data"] = nlohmann::json::array();
    item["next_sequence"] = chatRoomNextSequence(item);
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
        if (allowed) {
            filtered[item_id] = item;
            // Chat history has its own bounded, sequence-based query. Keep the
            // initial snapshot small while still exposing room metadata.
            if (isChatRoomItem(filtered[item_id])) filtered[item_id].erase("data");
        }
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

static nlohmann::json publicRtcPeer(const PerSocketData* socket) {
    nlohmann::json groups = nlohmann::json::array();
    for (const auto& group : socket->groups) groups.push_back(group);
    return {
        {"peer_id", socket->rtc_peer_id},
        {"nickname", socket->nickname},
        {"tag_number", socket->tag_number},
        {"groups", std::move(groups)},
        {"is_admin", socket->is_admin}
    };
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
    if (required.size() <= socket->groups.size()) {
        for (const auto& group : required) if (socket->groups.count(group) > 0) return true;
    } else {
        for (const auto& group : socket->groups) if (required.count(group) > 0) return true;
    }
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

struct CanvasSettingsTaskResult {
    nlohmann::json response;
    nlohmann::json changed_settings;
    std::unordered_set<int> participant_ids;
    int removed_user_id{0};
    bool changed{false};
    bool canvas_name_changed{false};
    std::string canvas_name;
};

static CanvasSettingsTaskResult executeCanvasSettingsRequest(
        CanvasPool& pool, const std::shared_ptr<Canvas>& canvas, int canvas_id,
        int user_id, const std::string& nickname, int tag_number,
        const nlohmann::json& event, const std::string& request_id) {
    CanvasSettingsTaskResult result;
    auto reject = [&result, &request_id](const char* code) {
        result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                         {"request_id", request_id}, {"code", code}};
        return result;
    };
    if (!canvas) return reject("CANVAS_NOT_READY");

    RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
    const std::string key = "canvas:" + std::to_string(canvas_id);
    auto raw = redis.get(key);
    if (!raw) return reject("SETTINGS_STORAGE_ERROR");
    nlohmann::json doc;
    try { doc = nlohmann::json::parse(*raw); } catch (...) { return reject("SETTINGS_STORAGE_ERROR"); }
    if (!doc.is_object()) return reject("SETTINGS_STORAGE_ERROR");

    MssqlClient db(pool.getDbHost(), pool.getDbPort());
    if (!db.isCanvasAssignedToServer(canvas_id, pool.getCppServerIp(), pool.getCppServerPort())) {
        return reject("CANVAS_NOT_READY");
    }
    if (db.getActiveUserId(nickname, tag_number) != user_id
        || !isCanvasParticipant(doc, user_id)) return reject("SETTINGS_ACCESS_DENIED");

    const std::string type = event.value("type", "");
    if (type == "canvas_settings_get") {
        result.response = nlohmann::json{{"type", "canvas_settings_snapshot"},
                                         {"settings", canvasSettingsSnapshot(doc, db)}};
        return result;
    }
    if (type != "canvas_settings_update") return reject("SETTINGS_INVALID_INPUT");
    if (!groupsForUser(doc, user_id).second) return reject("SETTINGS_ACCESS_DENIED");
    if (!event.contains("expected_revision") || !event["expected_revision"].is_number_integer()) {
        return reject("SETTINGS_REVISION_REQUIRED");
    }

    const long long revision = doc.value("settings-revision", 0LL);
    if (event["expected_revision"].get<long long>() != revision) {
        result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
            {"request_id", request_id}, {"code", "SETTINGS_CONFLICT"},
            {"settings", canvasSettingsSnapshot(doc, db)}};
        return result;
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
        const std::string target_nickname = event["nickname"].get<std::string>();
        const int target_tag = event["tag_number"].get<int>();
        if (target_nickname.empty() || target_nickname.size() > 200 || target_tag < 1) {
            return reject("SETTINGS_INVALID_INPUT");
        }
        if (!doc.contains("people") || !doc["people"].is_array()) return reject("SETTINGS_STORAGE_ERROR");
        if (!doc.contains("inner-group") || !doc["inner-group"].is_object()) return reject("SETTINGS_STORAGE_ERROR");
        rememberOriginal("people");
        rememberOriginal("inner-group");
        int target_id = -1;
        if (field == "participant_add") {
            target_id = db.getActiveUserId(target_nickname, target_tag);
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
                if (handle && handle->first == target_nickname && handle->second == target_tag) {
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
                (void)group;
                if (!members.is_array()) continue;
                nlohmann::json updated = nlohmann::json::array();
                for (const auto& member : members) {
                    if (!member.is_number_integer() || member.get<int>() != target_id) updated.push_back(member);
                }
                members = std::move(updated);
            }
            result.removed_user_id = target_id;
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
        result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
            {"request_id", request_id}, {"code", "SETTINGS_CONFLICT"},
            {"settings", canvasSettingsSnapshot(latest, db)}};
        return result;
    }
    if (stored != RedisClient::CompareSetResult::Applied) return reject("SETTINGS_STORAGE_ERROR");

    EsClient es(pool.getEsHost(), pool.getEsPort());
    if (!es.patchCanvasFields(canvas_id, es_changes)) {
        std::vector<std::pair<std::string, nlohmann::json>> rollback;
        std::map<std::string, nlohmann::json> restore_in_es;
        for (const auto& [document_field, updated_value] : es_changes) {
            (void)updated_value;
            const nlohmann::json original_value = document_field == "settings-revision"
                ? nlohmann::json(revision)
                : (original_settings.find(document_field) != original_settings.end()
                    ? original_settings.at(document_field) : nlohmann::json(nullptr));
            rollback.emplace_back(redis_paths.at(document_field), original_value);
            restore_in_es[document_field] = original_value;
        }
        if (redis.compareAndSetJsonPaths(key, revision + 1, rollback) != RedisClient::CompareSetResult::Applied) {
            std::cerr << "[uWebSockets] Could not roll back Redis canvas settings after Elasticsearch failure for Canvas #"
                      << canvas_id << "\n";
        }
        if (!es.patchCanvasFields(canvas_id, restore_in_es)) {
            std::cerr << "[uWebSockets] Could not compensate Elasticsearch settings after a failed canvas update for Canvas #"
                      << canvas_id << "\n";
        }
        return reject("SETTINGS_STORAGE_ERROR");
    }

    canvas->setSettingsRevision(revision + 1);
    if (field == "name") {
        result.canvas_name_changed = true;
        result.canvas_name = doc["canvas-name"].get<std::string>();
    }
    if (doc.contains("people") && doc["people"].is_array()) {
        for (const auto& member : doc["people"]) {
            if (member.is_number_integer()) result.participant_ids.insert(member.get<int>());
        }
    }
    const auto snapshot = canvasSettingsSnapshot(doc, db);
    result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", true},
        {"request_id", request_id}, {"settings", snapshot}};
    result.changed_settings = snapshot;
    result.changed = true;
    return result;
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
            if (isChatRoomItem(filtered[item_id])) filtered[item_id].erase("data");
        }
    }
    return filtered;
}

WebSocketServer::WebSocketServer(CanvasPool& pool, const std::string& host, int ws_port, TokenValidator validator,
                                 const std::string& java_host, int java_port)
    : pool_(pool), host_(host), ws_port_(ws_port), token_validator_(std::move(validator)),
      java_host_(java_host), java_port_(java_port) {
    session_cleanup_thread_ = std::thread([this]() {
        pthread_setname_np(pthread_self(), "agora-disconn");
        for (;;) {
            int user_id = 0;
            int canvas_id = 0;
            std::uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(session_cleanup_mutex_);
                session_cleanup_cv_.wait(lock, [this]() {
                    return session_cleanup_stopping_ || !session_cleanup_order_.empty();
                });
                if (session_cleanup_order_.empty() && session_cleanup_stopping_) break;
                user_id = session_cleanup_order_.front();
                session_cleanup_order_.pop_front();
                const auto pending = session_cleanup_pending_.find(user_id);
                if (pending == session_cleanup_pending_.end()) continue;
                canvas_id = pending->second.first;
                generation = pending->second.second;
                session_cleanup_pending_.erase(pending);
            }
            try {
                pool_.updateUserSessionDisconnected(user_id, canvas_id, generation);
            } catch (const std::exception& e) {
                std::cerr << "[uWebSockets] Failed to update user disconnect state: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[uWebSockets] Failed to update user disconnect state\n";
            }
        }
    });
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
    {
        std::lock_guard<std::mutex> lock(session_cleanup_mutex_);
        session_cleanup_stopping_ = true;
    }
    session_cleanup_cv_.notify_one();
    if (session_cleanup_thread_.joinable()) session_cleanup_thread_.join();
    pool_.setWebSocketCallbacks({});
}

bool WebSocketServer::beginBlockingWorker() {
    constexpr int MAX_BLOCKING_WORKERS = 8;
    std::lock_guard<std::mutex> lock(worker_mutex_);
    if (active_blocking_workers_ >= MAX_BLOCKING_WORKERS) return false;
    ++active_blocking_workers_;
    ++active_workers_;
    return true;
}

void WebSocketServer::endBlockingWorker() {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    --active_blocking_workers_;
    if (--active_workers_ == 0) worker_cv_.notify_all();
}

void WebSocketServer::clearSessionAsync(int user_id, int canvas_id, std::uint64_t session_generation) {
    {
        std::lock_guard<std::mutex> lock(session_cleanup_mutex_);
        if (session_cleanup_stopping_) return;
        const auto [it, inserted] = session_cleanup_pending_.try_emplace(
            user_id, canvas_id, session_generation);
        if (inserted) session_cleanup_order_.push_back(user_id);
        else if (session_generation > it->second.second) {
            it->second = {canvas_id, session_generation};
        }
    }
    session_cleanup_cv_.notify_one();
}

void WebSocketServer::refreshUserSessionGeneration(int canvas_id, int user_id,
                                                   std::uint64_t session_generation) {
    auto it = sockets_by_canvas_.find(canvas_id);
    if (it == sockets_by_canvas_.end()) return;
    for (Socket* ws : it->second) {
        auto* data = ws->getUserData();
        if (data->user_id == user_id && data->access_authorized && !data->closing) {
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
                sockets_by_connection_id_.clear();
                sockets_by_canvas_group_.clear();
                admin_sockets_by_canvas_.clear();
                sockets_by_canvas_peer_.clear();
                item_permissions_by_canvas_.clear();
                chat_rooms_by_canvas_.clear();
                chat_next_sequence_by_canvas_.clear();
                authorization_epochs_by_canvas_.clear();

                for (Socket* ws : sockets) {
                    closeSocketSession(ws, 1001, "Server shutting down");
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
            if (!data->access_authorized || data->closing) continue;
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
            if (ws->getUserData()->access_authorized && !ws->getUserData()->closing
                && ws->getUserData()->user_id == user_id) {
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
            if (ws->getUserData()->user_id == user_id && !ws->getUserData()->closing) {
                sockets_to_close.push_back(ws);
            }
        }
        for (Socket* ws : sockets_to_close) {
            closeSocketSession(ws, 1008, "Access revoked");
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
            closeSocketSession(ws, 1008, "Canvas session ended");
        }
    });
}

void WebSocketServer::registerSocket(Socket* ws) {
    const PerSocketData* data = ws->getUserData();
    registered_sockets_.insert(ws);
    sockets_by_canvas_[data->canvas_id].insert(ws);
    sockets_by_connection_id_[data->connection_id] = ws;
}

WebSocketServer::Socket* WebSocketServer::findSocketByConnectionId(std::uint64_t connection_id) const {
    const auto it = sockets_by_connection_id_.find(connection_id);
    return it == sockets_by_connection_id_.end() ? nullptr : it->second;
}

void WebSocketServer::indexSocket(Socket* ws) {
    const auto* data = ws->getUserData();
    if (!data->access_authorized || data->closing) return;
    if (data->is_admin) admin_sockets_by_canvas_[data->canvas_id].insert(ws);
    auto& groups = sockets_by_canvas_group_[data->canvas_id];
    for (const auto& group : data->groups) groups[group].insert(ws);
}

void WebSocketServer::unindexSocket(Socket* ws) {
    const auto* data = ws->getUserData();
    auto admins = admin_sockets_by_canvas_.find(data->canvas_id);
    if (admins != admin_sockets_by_canvas_.end()) {
        admins->second.erase(ws);
        if (admins->second.empty()) admin_sockets_by_canvas_.erase(admins);
    }
    auto canvas_groups = sockets_by_canvas_group_.find(data->canvas_id);
    if (canvas_groups == sockets_by_canvas_group_.end()) return;
    for (const auto& group : data->groups) {
        auto group_sockets = canvas_groups->second.find(group);
        if (group_sockets == canvas_groups->second.end()) continue;
        group_sockets->second.erase(ws);
        if (group_sockets->second.empty()) canvas_groups->second.erase(group_sockets);
    }
    if (canvas_groups->second.empty()) sockets_by_canvas_group_.erase(canvas_groups);
}

std::unordered_set<WebSocketServer::Socket*> WebSocketServer::socketsForGroups(
        int canvas_id, const std::unordered_set<std::string>& groups) const {
    std::unordered_set<Socket*> sockets;
    auto admins = admin_sockets_by_canvas_.find(canvas_id);
    if (admins != admin_sockets_by_canvas_.end()) {
        sockets.insert(admins->second.begin(), admins->second.end());
    }
    auto canvas_groups = sockets_by_canvas_group_.find(canvas_id);
    if (canvas_groups == sockets_by_canvas_group_.end()) return sockets;
    for (const auto& group : groups) {
        auto group_sockets = canvas_groups->second.find(group);
        if (group_sockets != canvas_groups->second.end()) {
            sockets.insert(group_sockets->second.begin(), group_sockets->second.end());
        }
    }
    return sockets;
}

void WebSocketServer::unregisterSocket(Socket* ws) {
    const PerSocketData* data = ws->getUserData();
    registered_sockets_.erase(ws);
    const auto connection = sockets_by_connection_id_.find(data->connection_id);
    if (connection != sockets_by_connection_id_.end() && connection->second == ws) {
        sockets_by_connection_id_.erase(connection);
    }
    unindexSocket(ws);
    if (!data->rtc_peer_id.empty()) {
        auto peers = sockets_by_canvas_peer_.find(data->canvas_id);
        if (peers != sockets_by_canvas_peer_.end()) {
            auto peer = peers->second.find(data->rtc_peer_id);
            if (peer != peers->second.end() && peer->second == ws) peers->second.erase(peer);
            if (peers->second.empty()) sockets_by_canvas_peer_.erase(peers);
        }
    }
    auto it = sockets_by_canvas_.find(data->canvas_id);
    if (it == sockets_by_canvas_.end()) return;

    it->second.erase(ws);
    if (it->second.empty()) {
        sockets_by_canvas_.erase(it);
        item_permissions_by_canvas_.erase(data->canvas_id);
        chat_rooms_by_canvas_.erase(data->canvas_id);
        chat_next_sequence_by_canvas_.erase(data->canvas_id);
        authorization_epochs_by_canvas_.erase(data->canvas_id);
        sockets_by_canvas_group_.erase(data->canvas_id);
        admin_sockets_by_canvas_.erase(data->canvas_id);
        sockets_by_canvas_peer_.erase(data->canvas_id);
    }
}

void WebSocketServer::detachRtcPeer(Socket* ws) {
    if (!ws) return;
    PerSocketData* data = ws->getUserData();
    if (data->rtc_peer_id.empty()) return;

    const std::string peer_id = data->rtc_peer_id;
    const auto peer_index = sockets_by_canvas_peer_.find(data->canvas_id);
    if (peer_index != sockets_by_canvas_peer_.end()) {
        const auto peer = peer_index->second.find(peer_id);
        if (peer != peer_index->second.end() && peer->second == ws) {
            peer_index->second.erase(peer);
        }
        if (peer_index->second.empty()) sockets_by_canvas_peer_.erase(peer_index);
    }
    data->rtc_peer_id.clear();

    const auto sockets = sockets_by_canvas_.find(data->canvas_id);
    if (sockets == sockets_by_canvas_.end()) return;
    const std::string payload = nlohmann::json{
        {"type", "rtc_peer_left"}, {"peer_id", peer_id}
    }.dump();
    for (Socket* other : sockets->second) {
        const auto* other_data = other->getUserData();
        if (other != ws && other_data->access_authorized && !other_data->closing) {
            other->send(payload, uWS::OpCode::TEXT);
        }
    }
}

void WebSocketServer::closeSocketSession(Socket* ws, int code, const std::string& reason) {
    if (!ws || registered_sockets_.count(ws) == 0) return;
    PerSocketData* data = ws->getUserData();
    if (data->closing) return;
    data->closing = true;
    detachRtcPeer(ws);
    unindexSocket(ws);
    ws->end(code, reason);
}

void WebSocketServer::handleCanvasSettings(Socket* ws, const nlohmann::json& event) {
    const PerSocketData identity = *ws->getUserData();
    const std::string request_id = event.contains("request_id") && event["request_id"].is_string()
        ? event["request_id"].get<std::string>().substr(0, 64) : "";
    const int canvas_id = identity.canvas_id;
    const int user_id = identity.user_id;
    const std::uint64_t connection_id = identity.connection_id;
    auto canvas = pool_.getCanvas(canvas_id);
    if (!canvas) {
        ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                {"request_id", request_id}, {"code", "CANVAS_NOT_READY"}}.dump(),
                 uWS::OpCode::TEXT);
        return;
    }
    std::string pending_removal_nickname;
    int pending_removal_tag = 0;
    bool pending_participant_removal = false;
    if (event.contains("field") && event["field"].is_string()
        && event["field"].get<std::string>() == "participant_remove"
        && event.contains("nickname") && event["nickname"].is_string()
        && event.contains("tag_number") && event["tag_number"].is_number_integer()) {
        try {
            const long long requested_tag = event["tag_number"].get<long long>();
            if (requested_tag >= std::numeric_limits<int>::min()
                && requested_tag <= std::numeric_limits<int>::max()) {
                pending_removal_nickname = event["nickname"].get<std::string>();
                pending_removal_tag = static_cast<int>(requested_tag);
                pending_participant_removal = true;
            }
        } catch (...) {}
    }
    if (!beginBlockingWorker()) {
        ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                {"request_id", request_id}, {"code", "SERVER_BUSY"}}.dump(),
                 uWS::OpCode::TEXT);
        return;
    }

    if (pending_participant_removal) {
        auto sockets = sockets_by_canvas_.find(canvas_id);
        if (sockets != sockets_by_canvas_.end()) {
            for (Socket* target : sockets->second) {
                auto* target_data = target->getUserData();
                if (target_data->nickname == pending_removal_nickname
                    && target_data->tag_number == pending_removal_tag) {
                    ++target_data->permission_update_pending_count;
                    target_data->permission_update_pending = true;
                }
            }
        }
    }

    try {
        std::thread([this, connection_id, canvas, canvas_id, user_id, nickname = identity.nickname,
                     tag_number = identity.tag_number, event, request_id,
                     pending_participant_removal, pending_removal_nickname, pending_removal_tag]() mutable {
            pthread_setname_np(pthread_self(), "agora-settings");
            CanvasSettingsTaskResult result;
            std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);
            if (canvas->unloading) {
                result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                    {"request_id", request_id}, {"code", "CANVAS_NOT_READY"}};
            } else {
                try {
                    result = executeCanvasSettingsRequest(pool_, canvas, canvas_id, user_id,
                        nickname, tag_number, event, request_id);
                } catch (const std::exception& e) {
                    std::cerr << "[uWebSockets] Canvas settings worker failed: " << e.what() << "\n";
                    result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                        {"request_id", request_id}, {"code", "SETTINGS_STORAGE_ERROR"}};
                } catch (...) {
                    result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                        {"request_id", request_id}, {"code", "SETTINGS_STORAGE_ERROR"}};
                }
            }
            std::string response_payload;
            std::string notification_payload;
            try {
                response_payload = result.response.dump();
                if (result.changed) {
                    notification_payload = nlohmann::json{
                        {"type", "canvas_settings_changed"},
                        {"settings", result.changed_settings}}.dump();
                }
            } catch (...) {
                response_payload = "{\"type\":\"canvas_settings_result\",\"ok\":false,\"code\":\"SETTINGS_STORAGE_ERROR\"}";
            }

            bool deferred = false;
            {
                std::lock_guard<std::mutex> loop_lock(loop_mutex_);
                if (running_ && loop_) {
                    loop_->defer([this, connection_id, canvas, canvas_id, user_id,
                                  result = std::move(result), response_payload = std::move(response_payload),
                                  notification_payload = std::move(notification_payload),
                                  pending_participant_removal,
                                  pending_removal_nickname, pending_removal_tag]() mutable {
                        if (result.canvas_name_changed) canvas->setCanvasName(result.canvas_name);
                        Socket* sender = findSocketByConnectionId(connection_id);
                        const auto sender_sockets = sockets_by_canvas_.find(canvas_id);
                        const bool sender_authorized = sender
                            && sender_sockets != sockets_by_canvas_.end()
                            && sender_sockets->second.count(sender) > 0
                            && sender->getUserData()->access_authorized
                            && !sender->getUserData()->closing
                            && sender->getUserData()->user_id == user_id;
                        if (sender_authorized) {
                            sender->send(response_payload, uWS::OpCode::TEXT);
                        }
                        auto sockets = sockets_by_canvas_.find(canvas_id);
                        if (pending_participant_removal && sockets != sockets_by_canvas_.end()) {
                            const std::vector<Socket*> targets(sockets->second.begin(), sockets->second.end());
                            for (Socket* target : targets) {
                                auto* target_data = target->getUserData();
                                const bool target_by_id = result.removed_user_id > 0
                                    && target_data->user_id == result.removed_user_id;
                                const bool target_by_handle = target_data->nickname == pending_removal_nickname
                                    && target_data->tag_number == pending_removal_tag;
                                if (!target_by_id && !target_by_handle) continue;
                                if (target_data->permission_update_pending_count > 0) {
                                    --target_data->permission_update_pending_count;
                                }
                                if (target_by_id) {
                                    closeSocketSession(target, 1008, "Canvas access revoked");
                                    continue;
                                }
                                target_data->permission_update_pending = target_data->access_authorized
                                    && target_data->permission_update_pending_count > 0;
                            }
                        }
                        if (result.changed) {
                            ++authorization_epochs_by_canvas_[canvas_id];
                            if (result.removed_user_id > 0) {
                                pool_.disconnectUser(canvas_id, result.removed_user_id);
                            }
                            const auto current_sockets = sockets_by_canvas_.find(canvas_id);
                            if (current_sockets != sockets_by_canvas_.end()) {
                                for (Socket* target : current_sockets->second) {
                                    const auto* target_data = target->getUserData();
                                    if (target != sender && target_data->access_authorized
                                        && !target_data->closing
                                        && result.participant_ids.count(target_data->user_id) > 0) {
                                        target->send(notification_payload, uWS::OpCode::TEXT);
                                    }
                                }
                            }
                        }
                        endBlockingWorker();
                    });
                    deferred = true;
                }
            }
            settings_lock.unlock();
            if (!deferred) endBlockingWorker();
        }).detach();
    } catch (...) {
        if (pending_participant_removal) {
            auto sockets = sockets_by_canvas_.find(canvas_id);
            if (sockets != sockets_by_canvas_.end()) {
                for (Socket* target : sockets->second) {
                    auto* target_data = target->getUserData();
                    if (target_data->nickname == pending_removal_nickname
                        && target_data->tag_number == pending_removal_tag) {
                        if (target_data->permission_update_pending_count > 0) {
                            --target_data->permission_update_pending_count;
                        }
                        target_data->permission_update_pending = target_data->permission_update_pending_count > 0;
                    }
                }
            }
        }
        endBlockingWorker();
        ws->send(nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                {"request_id", request_id}, {"code", "SERVER_BUSY"}}.dump(),
                 uWS::OpCode::TEXT);
    }
}

void WebSocketServer::handleChatEvent(Socket* ws, nlohmann::json event) {
    auto* identity = ws->getUserData();
    const std::string request_id = event.contains("request_id") && event["request_id"].is_string()
        ? event["request_id"].get<std::string>().substr(0, 64) : "";
    auto reject = [&](const char* code) {
        ws->send(nlohmann::json{{"type", "error"}, {"code", code},
                                {"request_id", request_id}}.dump(), uWS::OpCode::TEXT);
    };

    if (!event.contains("room_id") || !event["room_id"].is_string()) return reject("CHAT_INVALID_ROOM");
    const std::string room_id = event["room_id"].get<std::string>();
    if (room_id.empty() || room_id.size() > 128) return reject("CHAT_INVALID_ROOM");
    if (!event.contains("text") || !event["text"].is_string()) return reject("CHAT_INVALID_MESSAGE");
    const std::string text = event["text"].get<std::string>();
    if (text.empty() || text.size() > 4096) return reject("CHAT_INVALID_MESSAGE");
    if (event.contains("request_id") && !event["request_id"].is_string()) return reject("CHAT_INVALID_REQUEST_ID");

    auto& permissions = item_permissions_by_canvas_[identity->canvas_id];
    auto permission = permissions.find(room_id);
    const bool create_room = permission == permissions.end();
    auto& rooms = chat_rooms_by_canvas_[identity->canvas_id];
    auto& sequences = chat_next_sequence_by_canvas_[identity->canvas_id];
    std::unordered_set<std::string> room_groups;

    if (create_room) {
        // A first message creates the room item with only the sender's current
        // groups, matching the normal item-creation ACL rule.
        if (identity->groups.empty()) return reject("ITEM_ACCESS_DENIED");
        room_groups = identity->groups;
    } else {
        if (rooms.count(room_id) == 0) return reject("CHAT_ROOM_NOT_FOUND");
        room_groups = permission->second;
        if (!identity->is_admin && (room_groups.empty() || !hasAnyGroup(identity, room_groups))) {
            return reject("ITEM_ACCESS_DENIED");
        }
    }

    std::uint64_t sequence = create_room ? 1 : sequences[room_id];
    if (sequence == 0) sequence = 1;
    if (sequence == std::numeric_limits<std::uint64_t>::max()) return reject("CHAT_SEQUENCE_EXHAUSTED");
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    nlohmann::json room_permission = nlohmann::json::array();
    for (const auto& group : room_groups) room_permission.push_back(group);
    event = {
        {"type", "chat"}, {"room_id", room_id}, {"text", text},
        {"sender", identity->nickname}, {"tag_number", identity->tag_number},
        {"sender_user_id", identity->user_id},
        {"canvas_id", identity->canvas_id}, {"sequence", sequence}, {"created_at", timestamp}
    };
    if (!request_id.empty()) event["request_id"] = request_id;
    if (create_room) {
        event["room_created"] = true;
        event["room_permission"] = room_permission;
        permissions[room_id] = room_groups;
        rooms.insert(room_id);
    }
    sequences[room_id] = sequence + 1;

    auto canvas = pool_.getCanvas(identity->canvas_id);
    bool start_worker = false;
    if (!canvas || !canvas->enqueuePersistence(event, start_worker)) {
        if (create_room) {
            permissions.erase(room_id);
            rooms.erase(room_id);
            sequences.erase(room_id);
        } else {
            sequences[room_id] = sequence;
        }
        return reject("CHAT_STORAGE_UNAVAILABLE");
    }
    if (start_worker) {
        try {
            std::thread([canvas]() {
                pthread_setname_np(pthread_self(), "agora-persist");
                drainCanvasPersistenceQueue(canvas);
            }).detach();
        } catch (...) {
            canvas->cancelPersistenceQueue();
            if (create_room) {
                permissions.erase(room_id);
                rooms.erase(room_id);
                sequences.erase(room_id);
            } else {
                sequences[room_id] = sequence;
            }
            return reject("CHAT_STORAGE_UNAVAILABLE");
        }
    }

    const auto recipients = socketsForGroups(identity->canvas_id, room_groups);
    if (create_room) {
        ++authorization_epochs_by_canvas_[identity->canvas_id];
        nlohmann::json item = {
            {"type", "chat_room"}, {"permission", room_permission},
            {"data", nlohmann::json::array()}, {"next_sequence", sequence + 1}
        };
        const std::string created_item = nlohmann::json{
            {"type", "item_update"}, {"canvas_id", identity->canvas_id},
            {"item_id", room_id}, {"item", item}
        }.dump();
        bool sender_received_item = false;
        for (Socket* target : recipients) {
            const auto* target_data = target->getUserData();
            if (target_data->access_authorized && !target_data->closing) {
                target->send(created_item, uWS::OpCode::TEXT);
                if (target == ws) sender_received_item = true;
            }
        }
        if (!sender_received_item && identity->access_authorized && !identity->closing) {
            ws->send(created_item, uWS::OpCode::TEXT);
        }
    }

    // The persistence queue stores room_permission for the atomic first-write;
    // it is internal metadata and is not part of the public chat event.
    event.erase("room_permission");
    event.erase("sender_user_id");
    const std::string payload = event.dump();
    bool sender_received_message = false;
    for (Socket* target : recipients) {
        const auto* target_data = target->getUserData();
        if (target_data->access_authorized && !target_data->closing) {
            target->send(payload, uWS::OpCode::TEXT);
            if (target == ws) sender_received_message = true;
        }
    }
    if (!sender_received_message && identity->access_authorized && !identity->closing) {
        ws->send(payload, uWS::OpCode::TEXT);
    }
}

void WebSocketServer::handleChatHistoryRequest(Socket* ws, const nlohmann::json& event) {
    const PerSocketData identity = *ws->getUserData();
    const std::uint64_t connection_id = identity.connection_id;
    const std::string request_id = event.contains("request_id") && event["request_id"].is_string()
        ? event["request_id"].get<std::string>().substr(0, 64) : "";
    auto reject = [&](const char* code) {
        ws->send(nlohmann::json{{"type", "error"}, {"code", code},
                                {"request_id", request_id}}.dump(), uWS::OpCode::TEXT);
    };
    if (!event.contains("room_id") || !event["room_id"].is_string()) return reject("CHAT_INVALID_ROOM");
    const std::string room_id = event["room_id"].get<std::string>();
    if (room_id.empty() || room_id.size() > 128) return reject("CHAT_INVALID_ROOM");

    const auto rooms = chat_rooms_by_canvas_.find(identity.canvas_id);
    const auto canvas_permissions = item_permissions_by_canvas_.find(identity.canvas_id);
    if (rooms == chat_rooms_by_canvas_.end() || rooms->second.count(room_id) == 0
        || canvas_permissions == item_permissions_by_canvas_.end()) return reject("CHAT_ROOM_NOT_FOUND");
    const auto permission = canvas_permissions->second.find(room_id);
    if (permission == canvas_permissions->second.end()) return reject("CHAT_ROOM_NOT_FOUND");
    if (!identity.is_admin && (permission->second.empty() || !hasAnyGroup(&identity, permission->second))) {
        return reject("ITEM_ACCESS_DENIED");
    }

    auto read_sequence = [&](const char* field, bool& supplied, std::uint64_t& value) {
        supplied = event.contains(field);
        if (!supplied) return true;
        value = positiveSequence(event[field]);
        return value > 0;
    };
    bool has_from = false;
    bool has_to = false;
    std::uint64_t from_sequence = 0;
    std::uint64_t to_sequence = 0;
    if (!read_sequence("from_sequence", has_from, from_sequence)
        || !read_sequence("to_sequence", has_to, to_sequence)) return reject("CHAT_INVALID_RANGE");
    if ((has_from || has_to) && event.contains("limit")) return reject("CHAT_INVALID_RANGE");

    std::uint64_t limit = 50;
    if (event.contains("limit")) {
        limit = positiveSequence(event["limit"]);
        if (limit == 0 || limit > 200) return reject("CHAT_INVALID_LIMIT");
    }
    if (has_from && has_to && (from_sequence > to_sequence || to_sequence - from_sequence >= 200)) {
        return reject("CHAT_INVALID_RANGE");
    }

    const int canvas_id = identity.canvas_id;
    const int user_id = identity.user_id;
    const auto epoch_it = authorization_epochs_by_canvas_.find(canvas_id);
    const std::uint64_t authorization_epoch = epoch_it == authorization_epochs_by_canvas_.end()
        ? 0 : epoch_it->second;
    auto canvas = pool_.getCanvas(canvas_id);
    if (!canvas) return reject("CHAT_HISTORY_UNAVAILABLE");
    const std::uint64_t persistence_barrier = canvas->persistenceBarrier();
    if (!beginBlockingWorker()) return reject("SERVER_BUSY");

    try {
        std::thread([this, connection_id, canvas, canvas_id, user_id, room_id, request_id,
                     authorization_epoch, persistence_barrier, has_from, has_to,
                     from_sequence, to_sequence, limit]() {
            pthread_setname_np(pthread_self(), "agora-history");
            std::string response_payload;
            auto errorPayload = [&request_id](const char* code) {
                return nlohmann::json{{"type", "error"}, {"code", code},
                                      {"request_id", request_id}}.dump();
            };
            try {
                std::unique_lock<std::mutex> settings_lock(canvas->settings_mutex);
                canvas->waitForPersistenceThrough(persistence_barrier);
                if (canvas->unloading) {
                    response_payload = errorPayload("CHAT_HISTORY_UNAVAILABLE");
                } else {
                    RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
                    auto raw = redis.getChatHistoryPage("canvas:" + std::to_string(canvas_id), room_id,
                        has_from ? std::optional<std::uint64_t>(from_sequence) : std::nullopt,
                        has_to ? std::optional<std::uint64_t>(to_sequence) : std::nullopt, limit);
                    if (!raw || *raw == "BAD_HISTORY") {
                        response_payload = errorPayload("CHAT_HISTORY_UNAVAILABLE");
                    } else if (*raw == "ROOM_NOT_FOUND") {
                        response_payload = errorPayload("CHAT_ROOM_NOT_FOUND");
                    } else {
                        nlohmann::json page;
                        try { page = nlohmann::json::parse(*raw); }
                        catch (...) { page = nlohmann::json(); }
                        if (!page.is_object() || !page.contains("messages")) {
                            response_payload = errorPayload("CHAT_HISTORY_UNAVAILABLE");
                        } else {
                            nlohmann::json messages = page["messages"];
                            if (messages.is_object() && messages.empty()) messages = nlohmann::json::array();
                            if (!messages.is_array()) {
                                response_payload = errorPayload("CHAT_HISTORY_UNAVAILABLE");
                            } else {
                                // Redis may contain messages written before this field stopped
                                // being public. Keep it in storage, but never return it.
                                for (auto& message : messages) {
                                    if (message.is_object()) message.erase("sender_user_id");
                                }
                                const bool has_more = page.value("has_more", false);
                                const std::uint64_t total = page.contains("total")
                                    ? positiveSequence(page["total"]) : 0;
                                nlohmann::json response = {
                                    {"type", "chat_history"}, {"canvas_id", canvas_id},
                                    {"room_id", room_id}, {"request_id", request_id},
                                    {"total", total}, {"messages", std::move(messages)}, {"has_more", has_more}
                                };
                                const std::uint64_t actual_from = page.contains("from_sequence")
                                    ? positiveSequence(page["from_sequence"]) : 0;
                                const std::uint64_t actual_to = page.contains("to_sequence")
                                    ? positiveSequence(page["to_sequence"]) : 0;
                                if (actual_from > 0 && actual_to > 0) {
                                    response["from_sequence"] = actual_from;
                                    response["to_sequence"] = actual_to;
                                    if (has_more) {
                                        response[has_from && !has_to ? "next_from_sequence" : "next_to_sequence"] =
                                            has_from && !has_to ? actual_to + 1 : actual_from - 1;
                                    }
                                } else {
                                    if (has_from) response["from_sequence"] = from_sequence;
                                    if (has_to) response["to_sequence"] = to_sequence;
                                }
                                response_payload = response.dump();
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[uWebSockets] Chat history worker failed: " << e.what() << "\n";
                response_payload = errorPayload("CHAT_HISTORY_UNAVAILABLE");
            } catch (...) {
                response_payload = errorPayload("CHAT_HISTORY_UNAVAILABLE");
            }

            bool deferred = false;
            {
                std::lock_guard<std::mutex> loop_lock(loop_mutex_);
                if (running_ && loop_) {
                    loop_->defer([this, connection_id, canvas_id, user_id, request_id, authorization_epoch,
                                  response_payload = std::move(response_payload)]() {
                        Socket* requester = findSocketByConnectionId(connection_id);
                        const auto sockets = sockets_by_canvas_.find(canvas_id);
                        const auto current_epoch = authorization_epochs_by_canvas_.find(canvas_id);
                        const std::uint64_t current_value = current_epoch == authorization_epochs_by_canvas_.end()
                            ? 0 : current_epoch->second;
                        if (requester && sockets != sockets_by_canvas_.end()
                            && sockets->second.count(requester) > 0
                            && requester->getUserData()->access_authorized
                            && !requester->getUserData()->closing
                            && !requester->getUserData()->permission_update_pending
                            && requester->getUserData()->user_id == user_id) {
                            if (current_value == authorization_epoch) {
                                requester->send(response_payload, uWS::OpCode::TEXT);
                            } else {
                                requester->send(nlohmann::json{{"type", "error"},
                                    {"code", "ITEM_ACCESS_DENIED"},
                                    {"request_id", request_id}}.dump(), uWS::OpCode::TEXT);
                            }
                        }
                        endBlockingWorker();
                    });
                    deferred = true;
                }
            }
            if (!deferred) endBlockingWorker();
        }).detach();
    } catch (...) {
        endBlockingWorker();
        return reject("SERVER_BUSY");
    }
}

void WebSocketServer::runServer() {
    pthread_setname_np(pthread_self(), "agora-ws");
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

            std::string token = getQueryParam(query, "token");
            std::string client_ip = std::string(res->getRemoteAddressAsText());
            if (isLocalProxyPeer(client_ip)) {
                const auto forwarded_ip = req->getHeader("x-real-ip");
                if (!forwarded_ip.empty()) {
                    client_ip.assign(forwarded_ip.data(), forwarded_ip.size());
                }
            }

            if (!beginBlockingWorker()) {
                res->writeStatus("503 Service Unavailable")->end("Authentication workers are busy");
                return;
            }
            auto request_active = std::make_shared<std::atomic<bool>>(true);
            const std::string websocket_key(req->getHeader("sec-websocket-key"));
            const std::string websocket_protocol(req->getHeader("sec-websocket-protocol"));
            const std::string websocket_extensions(req->getHeader("sec-websocket-extensions"));
            auto* response = res;
            auto* websocket_context = context;
            response->onAborted([request_active]() {
                request_active->store(false);
            });
            response->pause();

            try {
                std::thread([this, response, websocket_context, request_active, canvas_id,
                             token = std::move(token), client_ip = std::move(client_ip),
                             websocket_key, websocket_protocol, websocket_extensions]() mutable {
                    pthread_setname_np(pthread_self(), "agora-auth");
                    std::optional<AuthenticatedUser> authenticated_user;
                    try {
                        if (token_validator_ && !token.empty()) {
                            authenticated_user = token_validator_(token, canvas_id, client_ip);
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[uWebSockets] WebSocket authentication failed: " << e.what() << "\n";
                    } catch (...) {}

                    bool deferred = false;
                    {
                        std::lock_guard<std::mutex> loop_lock(loop_mutex_);
                        if (running_ && loop_) {
                            loop_->defer([this, response, websocket_context, request_active, canvas_id,
                                          websocket_key, websocket_protocol, websocket_extensions,
                                          authenticated_user = std::move(authenticated_user)]() mutable {
                                if (!request_active->load()) {
                                    endBlockingWorker();
                                    return;
                                }
                                // Authentication paused the HTTP socket. Upgrade keeps its
                                // poll mask, so restore reads before it becomes a WebSocket;
                                // otherwise close frames and TCP FIN are never observed.
                                response->resume();
                                if (!authenticated_user || authenticated_user->user_id <= 0
                                    || authenticated_user->tag_number < 0) {
                                    std::cout << "[uWebSockets] Upgrade rejected: 401 Unauthorized (Invalid, missing, or unauthorized JWT token for canvas #"
                                              << canvas_id << ")" << std::endl;
                                    response->writeStatus("401 Unauthorized")
                                        ->end("Invalid, missing, or unauthorized JWT token");
                                    endBlockingWorker();
                                    return;
                                }
                                PerSocketData socket_data{
                                    createSocketConnectionId(),
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
                                    {},
                                    0,
                                    false,
                                    {}
                                };
                                response->template upgrade<PerSocketData>(
                                    std::move(socket_data), websocket_key, websocket_protocol,
                                    websocket_extensions, websocket_context);
                                endBlockingWorker();
                            });
                            deferred = true;
                        }
                    }
                    if (!deferred) endBlockingWorker();
                }).detach();
            } catch (...) {
                endBlockingWorker();
                request_active->store(false);
                response->resume();
                response->writeStatus("503 Service Unavailable")
                    ->end("Authentication worker could not be started");
            }
        },

        .open = [this](auto* ws) {
            PerSocketData* data = ws->getUserData();
            std::cout << "[uWebSockets] WebSocket client connected: User #" << data->user_id
                      << " to Canvas #" << data->canvas_id << std::endl;

            registerSocket(ws);
            const std::uint64_t connection_id = data->connection_id;
            const int canvas_id = data->canvas_id;
            const int user_id = data->user_id;
            const long long token_revision = data->settings_revision;
            if (!beginBlockingWorker()) {
                closeSocketSession(ws, 1013, "Connection workers are busy");
                return;
            }
            try {
                std::thread([this, connection_id, canvas_id, user_id, token_revision]() {
                pthread_setname_np(pthread_self(), "agora-connect");
                std::shared_ptr<Canvas> canvas;
                nlohmann::json doc;
                std::unordered_set<std::string> prepared_groups;
                bool prepared_is_admin = false;
                std::unordered_map<std::string, std::unordered_set<std::string>> prepared_permissions;
                std::unordered_set<std::string> prepared_chat_rooms;
                std::unordered_map<std::string, std::uint64_t> prepared_chat_sequences;
                std::string init_payload;
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
                    const auto reservation = pool_.connectUserSession(user_id, canvas_id);
                    if (!reservation) {
                        close_code = 1011;
                        close_reason = "Canvas initialization or session reservation failed";
                    } else {
                        canvas = reservation->canvas;
                        session_connected = true;
                        session_generation = reservation->generation;
                    }
                }

                bool settings_unchanged = false;
                std::unique_lock<std::mutex> canvas_settings_lock;
                if (close_code == 0 && canvas) {
                    canvas_settings_lock = std::unique_lock<std::mutex>(canvas->settings_mutex);
                    if (!canvas->unloading) {
                        const auto persistence_barrier = canvas->persistenceBarrier();
                        canvas->waitForPersistenceThrough(persistence_barrier);
                        if (!canvas->unloading) {
                            RedisClient latest_redis(canvas->getRedisIp(), canvas->getRedisPort());
                            auto latest_raw = latest_redis.get("canvas:" + std::to_string(canvas_id));
                            if (latest_raw) {
                                try {
                                    auto latest_doc = nlohmann::json::parse(*latest_raw);
                                    settings_unchanged = latest_doc.is_object()
                                        && latest_doc.value("settings-revision", 0LL) == token_revision
                                        && canvas->getSettingsRevision() == token_revision;
                                    if (settings_unchanged) {
                                        doc = std::move(latest_doc);
                                        auto user_membership = groupsForUser(doc, user_id);
                                        prepared_groups = std::move(user_membership.first);
                                        prepared_is_admin = user_membership.second;
                                        nlohmann::json init_msg = {
                                            {"type", "init_items"}, {"canvas_id", canvas_id},
                                            {"server_protocol", "uWebSockets"}, {"status", "connected"},
                                            {"items", filterItemsForUser(doc, user_id)}
                                        };
                                        nlohmann::json visible_groups = nlohmann::json::array();
                                        for (const auto& group : prepared_groups) visible_groups.push_back(group);
                                        init_msg["groups"] = std::move(visible_groups);
                                        if (doc.contains("canvas-name")) init_msg["canvas_name"] = doc["canvas-name"];
                                        init_payload = init_msg.dump();

                                        if (doc.contains("items") && doc["items"].is_object()) {
                                            for (const auto& [item_id, item] : doc["items"].items()) {
                                                prepared_permissions[item_id] = permissionGroups(item);
                                                if (isChatRoomItem(item)) {
                                                    prepared_chat_rooms.insert(item_id);
                                                    prepared_chat_sequences[item_id] = chatRoomNextSequence(item);
                                                }
                                            }
                                        }
                                    }
                                } catch (...) { settings_unchanged = false; }
                            }
                        }
                    }
                }

                std::lock_guard<std::mutex> loop_lock(loop_mutex_);
                if (!running_ || !loop_) {
                    if (canvas_settings_lock.owns_lock()) canvas_settings_lock.unlock();
                    if (session_connected && (!canvas || !canvas->isUserActive(user_id))) {
                        pool_.updateUserSessionDisconnected(user_id, canvas_id, session_generation);
                    }
                    endBlockingWorker();
                    return;
                }

                loop_->defer([this, connection_id, canvas_id, user_id, token_revision, session_generation,
                              canvas, prepared_groups = std::move(prepared_groups), prepared_is_admin,
                              prepared_permissions = std::move(prepared_permissions),
                              prepared_chat_rooms = std::move(prepared_chat_rooms),
                              prepared_chat_sequences = std::move(prepared_chat_sequences),
                              init_payload = std::move(init_payload), close_code,
                              close_reason = std::move(close_reason), session_connected,
                              settings_unchanged]() mutable {
                    Socket* ws = findSocketByConnectionId(connection_id);
                    auto socket_it = sockets_by_canvas_.find(canvas_id);
                    const bool socket_exists = ws && !ws->getUserData()->closing
                        && socket_it != sockets_by_canvas_.end()
                        && socket_it->second.count(ws) > 0;
                    if (!socket_exists) {
                        if (session_connected) {
                            if (canvas && canvas->isUserActive(user_id)) {
                                refreshUserSessionGeneration(canvas_id, user_id, session_generation);
                            } else {
                                clearSessionAsync(user_id, canvas_id, session_generation);
                            }
                        }
                        endBlockingWorker();
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
                        closeSocketSession(ws, close_code, close_reason);
                        endBlockingWorker();
                        return;
                    }
                    settings_unchanged = settings_unchanged && !canvas->unloading
                        && canvas->getSettingsRevision() == token_revision;
                    if (settings_unchanged) {
                        canvas->connectUser(user_id, 0, 0);
                        ws->getUserData()->groups = std::move(prepared_groups);
                        ws->getUserData()->is_admin = prepared_is_admin;
                        bool has_existing_authorized_socket = false;
                        const auto current_sockets = sockets_by_canvas_.find(canvas_id);
                        if (current_sockets != sockets_by_canvas_.end()) {
                            for (Socket* other : current_sockets->second) {
                                if (other != ws && other->getUserData()->access_authorized
                                    && !other->getUserData()->closing) {
                                    has_existing_authorized_socket = true;
                                    break;
                                }
                            }
                        }
                        if (!has_existing_authorized_socket) {
                            auto& permissions = item_permissions_by_canvas_[canvas_id];
                            auto& chat_rooms = chat_rooms_by_canvas_[canvas_id];
                            auto& chat_sequences = chat_next_sequence_by_canvas_[canvas_id];
                            permissions = std::move(prepared_permissions);
                            chat_rooms = std::move(prepared_chat_rooms);
                            chat_sequences = std::move(prepared_chat_sequences);
                            ++authorization_epochs_by_canvas_[canvas_id];
                        }
                        ws->getUserData()->access_authorized = true;
                        indexSocket(ws);
                    }
                    if (!settings_unchanged) {
                        if (canvas->isUserActive(user_id)) {
                            refreshUserSessionGeneration(canvas_id, user_id, session_generation);
                        } else {
                            clearSessionAsync(user_id, canvas_id, session_generation);
                        }
                        closeSocketSession(ws, 1008, "Canvas settings changed; request a new access token");
                        endBlockingWorker();
                        return;
                    }

                    ws->getUserData()->session_generation = session_generation;
                    refreshUserSessionGeneration(canvas_id, user_id, session_generation);
                    ws->subscribe("canvas/" + std::to_string(canvas_id));
                    const auto init_send_status = ws->send(init_payload, uWS::OpCode::TEXT);
                    if (init_send_status == Socket::DROPPED) {
                        std::cerr << "[uWebSockets] Dropped init_items for User #" << user_id
                                  << " on Canvas #" << canvas_id << " (bytes=" << init_payload.size() << ")\n";
                        closeSocketSession(ws, 1013, "Initial canvas state could not be delivered");
                        endBlockingWorker();
                        return;
                    }
                    auto* joined_data = ws->getUserData();
                    joined_data->rtc_peer_id = createRtcPeerId();
                    sockets_by_canvas_peer_[canvas_id][joined_data->rtc_peer_id] = ws;
                    nlohmann::json rtc_peers = nlohmann::json::array();
                    const auto connected_sockets = sockets_by_canvas_.find(canvas_id);
                    if (connected_sockets != sockets_by_canvas_.end()) {
                        for (Socket* other : connected_sockets->second) {
                            if (other == ws) continue;
                            PerSocketData* other_data = other->getUserData();
                            if (!other_data->access_authorized || other_data->closing
                                || other_data->permission_update_pending
                                || other_data->rtc_peer_id.empty()) continue;
                            rtc_peers.push_back(publicRtcPeer(other_data));
                            other->send(nlohmann::json{
                                {"type", "rtc_peer_joined"},
                                {"peer", publicRtcPeer(joined_data)}
                            }.dump(), uWS::OpCode::TEXT);
                        }
                    }
                    ws->send(nlohmann::json{
                        {"type", "rtc_peers"},
                        {"self_peer_id", joined_data->rtc_peer_id},
                        {"peers", std::move(rtc_peers)}
                    }.dump(), uWS::OpCode::TEXT);
                    std::cout << "[uWebSockets] Queued init_items for User #" << user_id
                              << " on Canvas #" << canvas_id << " (bytes=" << init_payload.size()
                              << ", buffered=" << (init_send_status == Socket::BACKPRESSURE) << ")\n";
                    endBlockingWorker();
                });
                }).detach();
            } catch (...) {
                endBlockingWorker();
                closeSocketSession(ws, 1013, "Connection worker could not be started");
            }
        },

        .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
            PerSocketData* data = ws->getUserData();
            if (data->closing) return;
            if (!data->access_authorized) {
                closeSocketSession(ws, 1008, "Canvas access is not authorized");
                return;
            }
            if (data->permission_update_pending) {
                ws->send(nlohmann::json{{"type", "error"},
                    {"code", "SETTINGS_UPDATE_PENDING"}}.dump(), uWS::OpCode::TEXT);
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
                        event.erase("sender_user_id");
                        event.erase("self_user_id");
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
                        const std::string event_type = event.value("type", "");
                        if (event_type == "rtc_disconnect") {
                            closeSocketSession(ws, 1000, "WebRTC session ended");
                            return;
                        }
                        if (event_type == "rtc_signal") {
                            const std::string target_peer_id = event.value("peer_id", "");
                            const std::string action = event.value("action", "");
                            const nlohmann::json* signal_payload = nullptr;
                            std::string payload_field;
                            if (action == "offer" || action == "answer") {
                                if (event.contains("description") && event["description"].is_object()
                                    && event["description"].contains("sdp") && event["description"]["sdp"].is_string()
                                    && event["description"]["sdp"].get<std::string>().size() <= 131072) {
                                    signal_payload = &event["description"];
                                    payload_field = "description";
                                }
                            } else if (action == "candidate") {
                                if (event.contains("candidate") && event["candidate"].is_object()
                                    && event["candidate"].dump().size() <= 8192) {
                                    signal_payload = &event["candidate"];
                                    payload_field = "candidate";
                                }
                            }
                            if (target_peer_id.empty() || target_peer_id.size() > 96 || !signal_payload) {
                                ws->send(nlohmann::json{{"type", "error"}, {"code", "RTC_SIGNAL_INVALID"}}.dump(), uWS::OpCode::TEXT);
                                return;
                            }
                            Socket* target = nullptr;
                            const auto peer_index = sockets_by_canvas_peer_.find(data->canvas_id);
                            if (peer_index != sockets_by_canvas_peer_.end()) {
                                const auto peer = peer_index->second.find(target_peer_id);
                                if (peer != peer_index->second.end()) {
                                    Socket* candidate = peer->second;
                                    const auto* candidate_data = candidate->getUserData();
                                    if (candidate != ws && candidate_data->access_authorized
                                        && !candidate_data->closing
                                        && !candidate_data->permission_update_pending) target = candidate;
                                }
                            }
                            if (!target) {
                                ws->send(nlohmann::json{{"type", "error"}, {"code", "RTC_PEER_NOT_FOUND"}}.dump(), uWS::OpCode::TEXT);
                                return;
                            }
                            const nlohmann::json source = publicRtcPeer(data);
                            nlohmann::json forwarded = {
                                {"type", "rtc_signal"},
                                {"action", action},
                                {"from_peer_id", data->rtc_peer_id},
                                {"from_nickname", source["nickname"]},
                                {"from_tag_number", source["tag_number"]},
                                {"from_groups", source["groups"]},
                                {"from_is_admin", source["is_admin"]},
                                {payload_field, *signal_payload}
                            };
                            target->send(forwarded.dump(), uWS::OpCode::TEXT);
                            return;
                        }
                        if (event_type.rfind("rtc_", 0) == 0) {
                            ws->send(nlohmann::json{{"type", "error"}, {"code", "RTC_SIGNAL_INVALID"}}.dump(), uWS::OpCode::TEXT);
                            return;
                        }
                        if (event_type == "item_crdt_change") {
                            ws->send(nlohmann::json{{"type", "error"}, {"code", "ITEM_SAVE_REQUIRED"}}.dump(), uWS::OpCode::TEXT);
                            return;
                        }
                        nlohmann::json* collaborative_payload = nullptr;
                        if (event.contains("item") && event["item"].is_object()) collaborative_payload = &event["item"];
                        else if (event.contains("data") && event["data"].is_object()) collaborative_payload = &event["data"];
                        if (collaborative_payload && event_type != "item_save"
                            && event_type != "item_delete" && event_type != "delete_item") {
                            const std::string item_kind = collaborative_payload->value("kind", "");
                            if (item_kind == "text" || item_kind == "note" || item_kind == "code"
                                || collaborative_payload->contains("text") || collaborative_payload->contains("code")
                                || collaborative_payload->contains("automerge_snapshot")
                                || collaborative_payload->contains("automerge_changes")) {
                                ws->send(nlohmann::json{{"type", "error"}, {"code", "ITEM_SAVE_REQUIRED"}}.dump(), uWS::OpCode::TEXT);
                                return;
                            }
                        }
                        if (event_type == "item_save") {
                            const std::size_t serialized_item_save_size = event.dump().size();
                            const std::string item_kind = collaborative_payload
                                ? collaborative_payload->value("kind", "") : "";
                            if (!collaborative_payload
                                || (item_kind != "text" && item_kind != "note" && item_kind != "code")
                                || !collaborative_payload->contains("automerge_snapshot")
                                || !(*collaborative_payload)["automerge_snapshot"].is_string()
                                || (*collaborative_payload)["automerge_snapshot"].get<std::string>().size() > 12582912
                                || !collaborative_payload->contains("automerge_changes")
                                || !(*collaborative_payload)["automerge_changes"].is_array()
                                || (*collaborative_payload)["automerge_changes"].size() > 100000
                                || serialized_item_save_size > 12582912) {
                                ws->send(nlohmann::json{{"type", "error"}, {"code", "ITEM_SAVE_INVALID"}}.dump(), uWS::OpCode::TEXT);
                                return;
                            }
                            const std::string expected_field = item_kind == "code" ? "code" : "text";
                            for (const auto& change : (*collaborative_payload)["automerge_changes"]) {
                                if (!change.is_object() || !change.contains("field") || !change["field"].is_string()
                                    || change["field"].get<std::string>() != expected_field
                                    || !change.contains("change") || !change["change"].is_string()
                                    || change["change"].get<std::string>().empty()
                                    || change["change"].get<std::string>().size() > 1048576) {
                                    ws->send(nlohmann::json{{"type", "error"}, {"code", "ITEM_SAVE_INVALID"}}.dump(), uWS::OpCode::TEXT);
                                    return;
                                }
                            }
                        }
                        if (event_type == "chat") {
                            handleChatEvent(ws, std::move(event));
                            return;
                        }
                        if (event_type == "chat_history") {
                            handleChatHistoryRequest(ws, event);
                            return;
                        }
                        const bool bulk_items = event.contains("items") && event["items"].is_object();
                        if (bulk_items) {
                            for (const auto& [bulk_id, bulk_item] : event["items"].items()) {
                                (void)bulk_id;
                                const std::string bulk_kind = bulk_item.is_object() ? bulk_item.value("kind", "") : "";
                                if (bulk_kind == "text" || bulk_kind == "note" || bulk_kind == "code"
                                    || (bulk_item.is_object() && (bulk_item.contains("text") || bulk_item.contains("code")
                                        || bulk_item.contains("automerge_snapshot") || bulk_item.contains("automerge_changes")))) {
                                    ws->send(nlohmann::json{{"type", "error"}, {"code", "ITEM_SAVE_REQUIRED"}}.dump(), uWS::OpCode::TEXT);
                                    return;
                                }
                            }
                        }
                        const std::string item_key = eventItemKey(event);
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
                        if (!bulk_items) {
                            nlohmann::json* item_payload = nullptr;
                            if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
                            else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
                            if (item_payload) normalizeChatRoomItem(*item_payload);
                        } else {
                            for (auto& [id, item] : event["items"].items()) {
                                (void)id;
                                if (isChatRoomItem(item) && !item.contains("data")) {
                                    item["data"] = nlohmann::json::array();
                                }
                                normalizeChatRoomItem(item);
                            }
                        }
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
                                const auto rooms = chat_rooms_by_canvas_.find(data->canvas_id);
                                const bool existing_chat_room = rooms != chat_rooms_by_canvas_.end()
                                    && rooms->second.count(item_key) > 0;
                                if (existing_chat_room && !data->is_admin) item_allowed = false;
                                if (existing_chat_room && data->is_admin && !delete_item) {
                                    nlohmann::json* item_payload = nullptr;
                                    if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
                                    else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
                                    if (item_payload && isChatRoomItem(*item_payload) && !item_payload->contains("data")) {
                                        event["_preserve_chat_history"] = true;
                                        const auto sequences = chat_next_sequence_by_canvas_.find(data->canvas_id);
                                        if (sequences != chat_next_sequence_by_canvas_.end()) {
                                            const auto next = sequences->second.find(item_key);
                                            if (next != sequences->second.end()) (*item_payload)["next_sequence"] = next->second;
                                        }
                                    }
                                }
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
                                if (!data->is_admin && !delete_item) {
                                    nlohmann::json* item_payload = nullptr;
                                    if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
                                    else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
                                    if (item_payload && isChatRoomItem(*item_payload)) {
                                        (*item_payload)["data"] = nlohmann::json::array();
                                        (*item_payload)["next_sequence"] = 1;
                                    }
                                }
                                const bool item_exists = permissions.find(item_key) != permissions.end();
                                if (!item_exists && data->is_admin && !delete_item) {
                                    nlohmann::json* item_payload = nullptr;
                                    if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
                                    else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
                                    if (item_payload && isChatRoomItem(*item_payload) && !item_payload->contains("data")) {
                                        (*item_payload)["data"] = nlohmann::json::array();
                                        (*item_payload)["next_sequence"] = 1;
                                    }
                                }
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
                                closeSocketSession(ws, 1012, "Canvas is closing");
                                return;
                            }
                            if (start_worker) {
                                try {
                                    std::thread([canvas]() {
                                        pthread_setname_np(pthread_self(), "agora-persist");
                                        drainCanvasPersistenceQueue(canvas);
                                    }).detach();
                                } catch (...) {
                                    canvas->cancelPersistenceQueue();
                                    restoreItemPermissions();
                                    closeSocketSession(ws, 1012, "Canvas persistence is unavailable");
                                    return;
                                }
                            }
                        }
                        if (item_event) ++authorization_epochs_by_canvas_[data->canvas_id];
                        if (bulk_items) {
                            auto& rooms = chat_rooms_by_canvas_[data->canvas_id];
                            auto& sequences = chat_next_sequence_by_canvas_[data->canvas_id];
                            rooms.clear();
                            sequences.clear();
                            for (const auto& [id, item] : event["items"].items()) {
                                if (isChatRoomItem(item)) {
                                    rooms.insert(id);
                                    sequences[id] = chatRoomNextSequence(item);
                                }
                            }
                        } else if (!item_key.empty()) {
                            auto& rooms = chat_rooms_by_canvas_[data->canvas_id];
                            auto& sequences = chat_next_sequence_by_canvas_[data->canvas_id];
                            if (delete_item) {
                                rooms.erase(item_key);
                                sequences.erase(item_key);
                            } else {
                                const nlohmann::json* item_payload = nullptr;
                                if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
                                else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
                                if (item_payload && isChatRoomItem(*item_payload)) {
                                    rooms.insert(item_key);
                                    sequences[item_key] = chatRoomNextSequence(*item_payload);
                                } else {
                                    rooms.erase(item_key);
                                    sequences.erase(item_key);
                                }
                            }
                        }
                        event.erase("_preserve_chat_history");
                        if (bulk_items) {
                            for (auto& [id, item] : event["items"].items()) {
                                (void)id;
                                if (isChatRoomItem(item)) item.erase("data");
                            }
                        } else {
                            nlohmann::json* item_payload = nullptr;
                            if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
                            else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
                            if (item_payload && isChatRoomItem(*item_payload)) item_payload->erase("data");
                        }
                        const std::string payload = event.dump();
                        std::unordered_set<Socket*> target_sockets;
                        if (item_event) {
                            std::unordered_set<std::string> candidate_groups = delivery_groups;
                            candidate_groups.insert(previous_groups.begin(), previous_groups.end());
                            if (bulk_items) {
                                for (const auto& [id, item] : event["items"].items()) {
                                    (void)id;
                                    const auto required = permissionGroups(item);
                                    candidate_groups.insert(required.begin(), required.end());
                                }
                                for (const auto& [id, groups] : previous_canvas_permissions) {
                                    (void)id;
                                    candidate_groups.insert(groups.begin(), groups.end());
                                }
                            }
                            target_sockets = socketsForGroups(data->canvas_id, candidate_groups);
                        } else {
                            const auto sockets = sockets_by_canvas_.find(data->canvas_id);
                            if (sockets != sockets_by_canvas_.end()) target_sockets = sockets->second;
                        }
                        for (Socket* target : target_sockets) {
                                if (target == ws || !target->getUserData()->access_authorized
                                    || target->getUserData()->closing) continue;
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
            const std::string peer_id = data->rtc_peer_id;
            const bool access_authorized = data->access_authorized;
            const std::uint64_t session_generation = data->session_generation;
            if (access_authorized) detachRtcPeer(ws);
            unregisterSocket(ws);
            const auto peers = sockets_by_canvas_peer_.find(canvas_id);
            const std::size_t remaining_peers = peers == sockets_by_canvas_peer_.end()
                ? 0 : peers->second.size();

            const auto* socket_context = us_socket_context(0, reinterpret_cast<us_socket_t*>(ws));
            std::cout << "[uWebSockets] WebSocket client disconnected: User #" << user_id
                      << " from Canvas #" << canvas_id << " (close code: " << code
                      << ", peer_id=" << peer_id << ", remaining_peers=" << remaining_peers
                      << ", socket=" << static_cast<void*>(ws)
                      << ", context=" << static_cast<const void*>(socket_context) << ")" << std::endl;

            auto canvas = pool_.getCanvas(canvas_id);
            bool final_connection_closed = false;
            if (canvas && access_authorized) {
                final_connection_closed = canvas->disconnectUser(user_id);
            }

            // The background initializer clears reservations for rejected or
            // prematurely closed sockets. Only established sockets own a Canvas entry.
            if (access_authorized && (!canvas || final_connection_closed
                || !canvas->isUserActive(user_id))) {
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
