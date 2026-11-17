#pragma once
#include "service/CanvasAccessRules.hpp"
#include <stdexcept>

inline std::unordered_set<std::string> queryPermissionGroups(const nlohmann::json& item) {
    std::unordered_set<std::string> groups;
    if (!item.is_object() || !item.contains("permission")) return groups;
    const auto& permission = item["permission"];
    if (permission.is_string()) groups.insert(permission.get<std::string>());
    else if (permission.is_array()) { for (const auto& value : permission) if (value.is_string()) groups.insert(value.get<std::string>()); }
    else if (permission.is_object()) { for (const auto& entry : permission.items()) if(entry.value()==true||(entry.value().is_number()&&entry.value().get<double>()>0))groups.insert(entry.key()); }
    return groups;
}
inline bool queryCanAccess(const nlohmann::json& item, const std::unordered_set<std::string>& groups, bool admin) {
    if (admin) return true;
    if(!item.is_object())return false;
    if(currentCanvasAcl)return currentCanvasAcl->allows(item.value("permission",nlohmann::json()),groups);
    for (const auto& required : queryPermissionGroups(item)) if (groups.count(required)) return true;
    return false;
}
inline bool validQueryItemId(const std::string& id) {
    return !id.empty() && id.size() <= 128 && id != "__proto__" && id != "prototype" && id != "constructor";
}
inline nlohmann::json queryCommentPermission(const nlohmann::json& target,
        const std::unordered_set<std::string>& groups, bool admin) {
    if (!target.is_object() || target.value("type", "") == "chat_room"
        || target.value("kind", "") == "host-tombstone" || !queryCanAccess(target, groups, admin))
        throw std::invalid_argument("ITEM_ACCESS_DENIED");
    return target.value("permission", nlohmann::json::array());
}
inline nlohmann::json queryChatRoomBroadcast(const std::string& room_id,
        const nlohmann::json& permission, std::uint64_t next_sequence) {
    using Json = nlohmann::json;
    return Json{{"event", {{"type", "item_update"}, {"item_id", room_id},
        {"item", {{"type", "chat_room"}, {"permission", permission}, {"next_sequence", next_sequence}}}}},
        {"permission", permission}};
}
inline bool queryObjectLocked(const nlohmann::json& item) {
    return item.is_object() && item.contains("locked") && item["locked"].is_boolean() && item["locked"].get<bool>();
}
inline void queryCheckObjectLock(const nlohmann::json& previous, const nlohmann::json& input) {
    if (input.is_object() && input.contains("locked") && !input["locked"].is_boolean()) throw std::invalid_argument("ITEM_LOCK_INVALID");
    if (!queryObjectLocked(previous)) return;
    if (!input.is_object() || !input.contains("locked") || input["locked"] != false) throw std::invalid_argument("ITEM_LOCKED");
    auto before = previous, after = input;
    before.erase("locked"); after.erase("locked");
    for (auto* item : {&before, &after}) {
        item->erase("__host_version"); item->erase("__broker_revision");
    }
    if (before != after) throw std::invalid_argument("ITEM_LOCKED");
}
inline nlohmann::json queryUpdatedItem(const nlohmann::json& previous, const nlohmann::json& input,
        const std::unordered_set<std::string>& groups, bool admin, const std::unordered_set<std::string>* memberships=nullptr, bool execution_state=false) {
    if (!input.is_object() || input.empty()) throw std::invalid_argument("ITEM_INVALID");
    if (!previous.is_null() && !queryCanAccess(previous, groups, admin)) throw std::invalid_argument("ITEM_ACCESS_DENIED");
    if (!queryCanAccess(input, groups, admin)) throw std::invalid_argument("ITEM_ACCESS_DENIED");
    const auto input_groups = queryPermissionGroups(input);
    if (input_groups.empty()) throw std::invalid_argument("ITEM_PERMISSION_DENIED");
    if (!admin) {
        const auto previous_groups = previous.is_null() ? std::unordered_set<std::string>{} : queryPermissionGroups(previous);
        const auto& allowed_groups = memberships ? *memberships : groups;
        for (const auto& required : input_groups) {
            if (!allowed_groups.count(required) && !previous_groups.count(required)) throw std::invalid_argument("ITEM_PERMISSION_DENIED");
        }
    }
    if (input.value("type", "") == "chat_room" || (!previous.is_null() && previous.value("type", "") == "chat_room"))
        throw std::invalid_argument("CHAT_QUERY_REQUIRED");
    if(!execution_state)queryCheckObjectLock(previous, input);
    const auto kind = input.value("kind", "");
    if (kind.empty()) throw std::invalid_argument("ITEM_KIND_REQUIRED");
    if (kind == "text" || kind == "note" || kind == "code") {
        // API clients may perform an explicit whole-content replacement without
        // an Automerge binary. Browsers rebuild a CRDT from canonical text.
        if (!input.contains("automerge_snapshot") && !input.contains("automerge_changes")) {
            const auto field = kind == "code" ? "code" : "text";
            if (!input.contains(field) || !input[field].is_string()) throw std::invalid_argument("ITEM_SAVE_INVALID");
            return input;
        }
        if (!input.contains("automerge_snapshot") || !input["automerge_snapshot"].is_string()
            || !input.contains("automerge_changes") || !input["automerge_changes"].is_array()
            || input["automerge_changes"].size() > 100000) throw std::invalid_argument("ITEM_SAVE_INVALID");
        for (const auto& change : input["automerge_changes"])
            if (!change.is_object() || !change.contains("change") || !change["change"].is_string()
                || change["change"].get<std::string>().size() > 1048576 || change.value("field", "") != (kind == "code" ? "code" : "text"))
                throw std::invalid_argument("ITEM_SAVE_INVALID");
    }
    return input;
}
