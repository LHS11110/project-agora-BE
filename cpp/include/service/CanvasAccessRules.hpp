#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>

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
    return {std::move(groups), is_admin};
}

