#include "service/CanvasQueryService.hpp"
#include "service/CanvasQueryRules.hpp"
#include "service/CanvasSettingsService.hpp"
#include "service_memory/CanvasServiceMemory.hpp"
#include "service_memory/CanvasEventMemory.hpp"
#include "service_memory/CanvasItemRules.hpp"
#include "service_memory/RegistryServiceMemory.hpp"
#include <chrono>
#include <algorithm>

namespace {
using Json = nlohmann::json;
Json routing(const Json& input) {
    const auto route = input.value("route", Json::object());
    if (!route.is_object() || !route.contains("canvas_id") || !route["canvas_id"].is_number_integer()
        || route["canvas_id"].get<int>() <= 0 || !route.contains("connection_id") || !route["connection_id"].is_string()
        || route["connection_id"].get<std::string>().empty() || route["connection_id"].get<std::string>().size() > 128
        || !route.contains("request_id") || !route["request_id"].is_string() || route["request_id"].get<std::string>().empty() || route["request_id"].get<std::string>().size() > 128)
        throw std::invalid_argument("ROUTE_INVALID");
    return {{"canvas_id", route["canvas_id"]}, {"connection_id", route["connection_id"]}, {"request_id", route["request_id"]}};
}
Json envelope(const Json& route, Json response, Json broadcasts = Json::array()) {
    response["request_id"] = route["request_id"];
    return {{"route", route}, {"response", std::move(response)}, {"broadcasts", std::move(broadcasts)}};
}
std::optional<Json> readItem(CanvasServiceMemory& memory, int id, const std::string& item) {
    const auto raw = memory.readItem(id, item);
    if (!raw) throw std::runtime_error("STORAGE_UNAVAILABLE");
    const auto matches = Json::parse(*raw);
    if (!matches.is_array()) throw std::runtime_error("STORAGE_UNAVAILABLE");
    return matches.empty() ? std::nullopt : std::optional<Json>(matches.front());
}
}
Json CanvasQueryService::execute(const Json& input) {
    Json route = Json::object();
    try {
        route = routing(input);
        if (input.value("command", "") == "connect") return join(input);
        if (input.value("command", "") == "query") return query(input);
        throw std::invalid_argument("QUERY_COMMAND_INVALID");
    } catch (const std::invalid_argument& error) {
        return envelope(route, {{"type", "error"}, {"ok", false}, {"code", error.what()}});
    } catch (...) {
        return envelope(route, {{"type", "error"}, {"ok", false}, {"code", "QUERY_UNAVAILABLE"}});
    }
}
Json CanvasQueryService::join(const Json& input) {
    auto route = routing(input); int id = route["canvas_id"];
    if (input.value("ws_port", 0) != logical_ws_port_) throw std::invalid_argument("SERVER_ROUTE_INVALID");
    const auto principal = input.value("principal", Json::object());
    if (!principal.is_object() || principal.value("canvas_id", 0) != id)
        throw std::invalid_argument("CANVAS_ACCESS_UNAUTHORIZED");
    const auto nickname = principal.value("nickname", "");
    const auto tag = principal.value("tag_number", -1);
    const auto revision = principal.value("settings_revision", -1LL);
    if (nickname.empty() || tag < 0 || revision < 0) throw std::invalid_argument("CANVAS_ACCESS_UNAUTHORIZED");
    RegistryServiceMemory registry(pool_.getDbHost(), pool_.getDbPort());
    const auto user_id = registry.getActiveUserId(nickname, tag);
    if (user_id <= 0 || !pool_.isCanvasAccessAuthorized(id, user_id, revision))
        throw std::invalid_argument("CANVAS_ACCESS_UNAUTHORIZED");
    auto canvas = pool_.loadForQuery(id);
    if (!canvas) throw std::runtime_error("CANVAS_NOT_READY");
    std::lock_guard<std::mutex> lock(canvas->settings_mutex);
    if (canvas->unloading) throw std::runtime_error("CANVAS_NOT_READY");
    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    auto raw = memory.loadCanvas(id); if (!raw) throw std::runtime_error("STORAGE_UNAVAILABLE");
    const auto doc = Json::parse(*raw);
    if (doc.value("settings-revision", 0LL) != revision) throw std::invalid_argument("SETTINGS_CHANGED");
    auto [groups, admin] = groupsForUser(doc, user_id);
    Json visible = Json::object();
    if (doc.contains("items") && doc["items"].is_object()) for (const auto& entry : doc["items"].items())
        if (queryCanAccess(entry.value(), groups, admin)) {
            auto item = entry.value(); if (agora::canvas::isChatRoomItem(item)) item.erase("data"); visible[entry.key()] = std::move(item);
        }
    Json identity{{"user_id", user_id}, {"nickname", nickname}, {"tag_number", tag},
        {"groups", groups}, {"is_admin", admin}, {"settings_revision", revision}, {"canvas_id", id}};
    return {{"route", route}, {"identity", identity},
        {"response", {{"type", "init_items"}, {"canvas_id", id}, {"items", visible}, {"groups", groups},
                      {"canvas_name", doc.value("canvas-name", "")}, {"status", "connected"}, {"server_protocol", "Phoenix"}}}};
}
Json CanvasQueryService::query(const Json& input) {
    auto route = routing(input); int id = route["canvas_id"];
    // Only the authenticated broker socket invokes this handler. Identity is
    // established by broker JWT authentication and authoritative canvas lookup.
    const auto identity = input.value("identity", Json::object());
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!identity.is_object() || identity.value("canvas_id", 0) != id
        || identity.value("connection_id", "") != route["connection_id"].get<std::string>()
        || identity.value("expires_at", 0LL) <= now || identity.value("user_id", 0) <= 0
        || !identity.contains("groups") || !identity["groups"].is_array()
        || !identity.contains("is_admin") || !identity["is_admin"].is_boolean())
        throw std::invalid_argument("QUERY_UNAUTHORIZED");
    auto canvas = pool_.getCanvas(id);
    if (!canvas) throw std::invalid_argument("CANVAS_NOT_READY");
    std::unique_lock<std::mutex> lock(canvas->settings_mutex);
    if (canvas->unloading) throw std::invalid_argument("CANVAS_NOT_READY");
    canvas->touchQuery();
    if (canvas->getSettingsRevision() != identity.value("settings_revision", -1LL)) throw std::invalid_argument("SETTINGS_CHANGED");
    auto event = input.value("event", Json::object());
    if (!event.is_object()) throw std::invalid_argument("QUERY_INVALID");
    const auto type = event.value("type", "");
    auto groups = identity["groups"].get<std::unordered_set<std::string>>(); const bool admin = identity.value("is_admin", false);
    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    if (type == "broker_lease") return envelope(route, {{"type", "lease_result"}, {"ok", true}});
    if (type.rfind("canvas_settings_", 0) == 0) {
        auto result = CanvasSettingsService::execute(pool_, canvas, id, identity["user_id"], identity["nickname"], identity["tag_number"], event, route["request_id"]);
        Json broadcasts = Json::array();
        if (result.changed) broadcasts.push_back({{"event", {{"type", "server_reconnect"}, {"reason", "settings_changed"}, {"retry_after_ms", 100}}}, {"all", true}, {"close", true}});
        return envelope(route, result.response, broadcasts);
    }
    if (type == "chat" || type == "chat_history") {
        const auto room_id = event.value("room_id", ""); if (!validQueryItemId(room_id)) throw std::invalid_argument("CHAT_INVALID_ROOM");
        auto room = readItem(memory, id, room_id);
        if (room && (!agora::canvas::isChatRoomItem(*room) || !queryCanAccess(*room, groups, admin))) throw std::invalid_argument("ITEM_ACCESS_DENIED");
        if (type == "chat_history") {
            if (!room) return envelope(route, {{"type", "chat_history"}, {"room_id", room_id}, {"messages", Json::array()}, {"total", 0}, {"has_more", false}});
            std::optional<std::uint64_t> from, to;
            if (event.contains("from_sequence") && event["from_sequence"].is_number_integer() && event["from_sequence"].get<long long>() > 0) from = event["from_sequence"].get<std::uint64_t>();
            if (event.contains("to_sequence") && event["to_sequence"].is_number_integer() && event["to_sequence"].get<long long>() > 0) to = event["to_sequence"].get<std::uint64_t>();
            auto page = memory.readChatPage(id, room_id, from, to, event.value("limit", 50ULL));
            if (!page) throw std::runtime_error("STORAGE_UNAVAILABLE");
            auto response = Json::parse(*page); response["type"] = "chat_history"; response["room_id"] = room_id;
            if (response.contains("from_sequence")) response["next_to_sequence"] = response["from_sequence"].get<long long>() - 1;
            if (response.contains("messages")) for (auto& message : response["messages"]) message.erase("sender_user_id");
            return envelope(route, response);
        }
        const auto text = event.value("text", ""); if (text.empty() || text.size() > 4096 || groups.empty()) throw std::invalid_argument("CHAT_INVALID_MESSAGE");
        Json permission = room ? (*room)["permission"] : Json(groups);
        const auto sequence = room ? agora::canvas::chatRoomNextSequence(*room) : 1ULL;
        Json message{{"type", "chat"}, {"room_id", room_id}, {"text", text}, {"sender", identity["nickname"]},
            {"tag_number", identity["tag_number"]}, {"sender_user_id", identity["user_id"]}, {"sequence", sequence},
            {"created_at", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            {"request_id", route["request_id"]}};
        if (!room) { message["room_created"] = true; message["room_permission"] = permission; }
        if (!CanvasEventMemory::apply(canvas, message) || !memory.flushCanvas(id)) throw std::runtime_error("STORAGE_UNAVAILABLE");
        message.erase("sender_user_id"); message.erase("room_permission");
        Json broadcasts = Json::array({{{"event", message}, {"permission", permission}}});
        if (!room) broadcasts.insert(broadcasts.begin(), {{"event", {{"type", "item_update"}, {"item_id", room_id}, {"item", {{"type", "chat_room"}, {"permission", permission}, {"next_sequence", sequence + 1}}}}}, {"permission", permission}});
        return envelope(route, {{"type", "event_ack"}, {"ok", true}, {"event_type", type}}, broadcasts);
    }
    const bool batch = type == "host_item_batch" || type == "crud_batch";
    Json operations = batch ? event.value("changes", Json::array()) : Json::array({event});
    if (!operations.is_array() || operations.empty() || operations.size() > 512) throw std::invalid_argument("QUERY_BATCH_INVALID");
    Json prepared = Json::array(), broadcasts = Json::array(), results = Json::array();
    std::unordered_set<std::string> touched;
    for (auto operation : operations) {
        const auto item_id = operation.value("item_id", operation.value("id", ""));
        if (!validQueryItemId(item_id) || !touched.insert(item_id).second) throw std::invalid_argument("ITEM_ID_INVALID");
        auto previous = readItem(memory, id, item_id);
        const Json old = previous.value_or(Json());
        if (previous && !queryCanAccess(old, groups, admin)) throw std::invalid_argument("ITEM_ACCESS_DENIED");
        auto action = operation.value("action", ""); const auto operation_type = operation.value("type", "");
        if (action.empty()) action = operation_type == "item_delete" || operation_type == "delete_item" ? "delete" : "update";
        if (action == "read") {
            results.push_back({{"item_id", item_id}, {"item", old}}); continue;
        }
        if (action == "delete") {
            if (previous && agora::canvas::isChatRoomItem(old)) throw std::invalid_argument("CHAT_QUERY_REQUIRED");
            prepared.push_back({{"type", "item_delete"}, {"item_id", item_id}});
            broadcasts.push_back({{"event", {{"type", "item_delete"}, {"item_id", item_id}}}, {"permission", previous ? old.value("permission", Json::array()) : Json::array()}});
        } else if (action == "create" || action == "update" || action == "patch") {
            if (action == "create" && previous) throw std::invalid_argument("ITEM_ALREADY_EXISTS");
            if (action == "patch" && !previous) throw std::invalid_argument("ITEM_NOT_FOUND");
            Json item = operation.value("item", Json::object());
            if (action == "patch") {
                item = old; auto patch = operation.value("patch", Json::object());
                if (!patch.is_object()) throw std::invalid_argument("ITEM_PATCH_INVALID");
                item.update(patch);
                for (const auto& field : operation.value("unset", Json::array())) { if (!field.is_string()) throw std::invalid_argument("ITEM_PATCH_INVALID"); item.erase(field.get<std::string>()); }
            }
            item = queryUpdatedItem(old, item, groups, admin);
            prepared.push_back({{"type", operation_type == "item_save" ? "item_save" : "item_update"}, {"item_id", item_id}, {"item", item}});
            // Host sessions already broadcast accepted object changes through their data channels.
            if (type != "host_item_batch") broadcasts.push_back({{"event", {{"type", "item_update"}, {"item_id", item_id}, {"item", item}}}, {"permission", item["permission"]}});
        } else throw std::invalid_argument("QUERY_ACTION_INVALID");
    }
    std::size_t applied = 0;
    for (const auto& operation : prepared) {
        if (!CanvasEventMemory::apply(canvas, operation)) return envelope(route,
            {{"type", type == "host_item_batch" ? "host_batch_result" : "crud_result"}, {"ok", false}, {"code", "STORAGE_UNAVAILABLE"}, {"applied", applied}});
        ++applied;
    }
    if (!memory.flushCanvas(id)) return envelope(route, {{"type", type == "host_item_batch" ? "host_batch_result" : "crud_result"}, {"ok", false}, {"code", "STORAGE_UNAVAILABLE"}, {"applied", applied}});
    return envelope(route, {{"type", type == "host_item_batch" ? "host_batch_result" : "crud_result"}, {"ok", true}, {"applied", applied}, {"results", results}}, broadcasts);
}
