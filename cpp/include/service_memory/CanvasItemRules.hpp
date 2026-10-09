#pragma once
#include <nlohmann/json.hpp>
#include <limits>
#include <cstdint>
namespace agora::canvas {
inline bool isChatRoomItem(const nlohmann::json& item) {
    return item.is_object() && item.contains("type") && item["type"].is_string()
        && item["type"].get<std::string>() == "chat_room";
}

inline std::uint64_t positiveSequence(const nlohmann::json& value) {
    try {
        if (value.is_number_unsigned()) return value.get<std::uint64_t>();
        if (value.is_number_integer()) {
            const auto sequence = value.get<long long>();
            return sequence > 0 ? static_cast<std::uint64_t>(sequence) : 0;
        }
    } catch (...) {}
    return 0;
}

inline std::uint64_t chatRoomNextSequence(const nlohmann::json& item) {
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

inline void normalizeChatRoomItem(nlohmann::json& item) {
    if (!isChatRoomItem(item)) return;
    if (item.contains("data") && !item["data"].is_array()) item["data"] = nlohmann::json::array();
    item["next_sequence"] = chatRoomNextSequence(item);
}

}
