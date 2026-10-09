#pragma once
#include "service_memory/CanvasLifecycleMemory.hpp"
#include <unordered_set>
struct CanvasSettingsTaskResult {
    nlohmann::json response;
    nlohmann::json changed_settings;
    std::unordered_set<int> participant_ids;
    int removed_user_id{0};
    bool changed{false};
    bool canvas_name_changed{false};
    std::string canvas_name;
};

class CanvasSettingsService final {
public:
    static CanvasSettingsTaskResult execute(CanvasLifecycleMemory& pool, const std::shared_ptr<Canvas>& canvas,
        int canvas_id, int user_id, const std::string& nickname, int tag_number,
        const nlohmann::json& event, const std::string& request_id);
};
