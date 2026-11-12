#pragma once
#include "service/CanvasAccessRules.hpp"
#include <stdexcept>
#include <algorithm>

// Called only after the broker has verified the canvas access token and active user.
inline bool enrollPublicParticipant(nlohmann::json& doc, int userId) {
    if (!canvasIsPublic(doc) || canvasHasMember(doc, userId)) return false;
    if (userId <= 0 || !doc.contains("people") || !doc["people"].is_array()
        || !doc.contains("inner-group") || !doc["inner-group"].is_object()
        || !doc.contains("init-group") || !doc["init-group"].is_string())
        throw std::invalid_argument("CANVAS_READ_DENIED");
    const auto group = doc["init-group"].get<std::string>();
    if (group.empty() || group == "admin-group" || !doc["inner-group"].contains(group)
        || !doc["inner-group"][group].is_array() || !canvasGroupCapability(doc, group, "read"))
        throw std::invalid_argument("CANVAS_READ_DENIED");
    doc["people"].push_back(userId);
    auto& members = doc["inner-group"][group];
    if (std::find(members.begin(), members.end(), nlohmann::json(userId)) == members.end()) members.push_back(userId);
    return true;
}
