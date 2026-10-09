#pragma once
#include "service/CanvasAccessRules.hpp"
#include <stdexcept>

inline std::unordered_set<std::string> queryPermissionGroups(const nlohmann::json& item) {
    std::unordered_set<std::string> groups;
    if (!item.is_object() || !item.contains("permission")) return groups;
    const auto& permission = item["permission"];
    if (permission.is_string()) groups.insert(permission.get<std::string>());
    else if (permission.is_array()) { for (const auto& value : permission) if (value.is_string()) groups.insert(value.get<std::string>()); }
    else if (permission.is_object()) { for (const auto& entry : permission.items()) groups.insert(entry.key()); }
    return groups;
}
inline bool queryCanAccess(const nlohmann::json& item, const std::unordered_set<std::string>& groups, bool admin) {
    if (admin) return true;
    for (const auto& required : queryPermissionGroups(item)) if (groups.count(required)) return true;
    return false;
}
inline bool validQueryItemId(const std::string& id) {
    return !id.empty() && id.size() <= 128 && id != "__proto__" && id != "prototype" && id != "constructor";
}
inline nlohmann::json queryUpdatedItem(const nlohmann::json& previous, const nlohmann::json& input,
        const std::unordered_set<std::string>& groups, bool admin) {
    if (!input.is_object() || input.empty()) throw std::invalid_argument("ITEM_INVALID");
    if (!previous.is_null() && !queryCanAccess(previous, groups, admin)) throw std::invalid_argument("ITEM_ACCESS_DENIED");
    if (!queryCanAccess(input, groups, admin)) throw std::invalid_argument("ITEM_ACCESS_DENIED");
    if (!admin && !previous.is_null() && queryPermissionGroups(previous) != queryPermissionGroups(input))
        throw std::invalid_argument("ITEM_PERMISSION_DENIED");
    if (!admin && previous.is_null()) for (const auto& required : queryPermissionGroups(input))
        if (!groups.count(required)) throw std::invalid_argument("ITEM_PERMISSION_DENIED");
    if (input.value("type", "") == "chat_room" || (!previous.is_null() && previous.value("type", "") == "chat_room"))
        throw std::invalid_argument("CHAT_QUERY_REQUIRED");
    const auto kind = input.value("kind", "");
    if (kind.empty()) throw std::invalid_argument("ITEM_KIND_REQUIRED");
    if (kind == "text" || kind == "note" || kind == "code") {
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
