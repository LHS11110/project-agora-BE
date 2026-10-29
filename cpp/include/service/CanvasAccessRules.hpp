#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>

inline bool canvasIsPublic(const nlohmann::json& doc) {
    if (!doc.is_object() || !doc.contains("visibility") || !doc["visibility"].is_string() || doc["visibility"] != "public") return false;
    if (!doc.contains("canvas-password-hash") || doc["canvas-password-hash"].is_null()) return true;
    return doc["canvas-password-hash"].is_string() && doc["canvas-password-hash"].get<std::string>().empty();
}

inline bool canvasHasMember(const nlohmann::json& doc, int user_id) {
    if (!doc.is_object() || !doc.contains("people") || !doc["people"].is_array()) return false;
    for (const auto& member : doc["people"]) if (member.is_number_integer() && member == user_id) return true;
    return false;
}

inline std::pair<std::unordered_set<std::string>, bool> groupsForUser(const nlohmann::json& doc, int user_id) {
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
    if (canvasIsPublic(doc) && doc.contains("init-group") && doc["init-group"].is_string()) {
        auto group = doc["init-group"].get<std::string>();
        if (!group.empty() && group != "admin-group" && doc["inner-group"].contains(group)) groups.insert(group);
    }
    return {std::move(groups), is_admin};
}

