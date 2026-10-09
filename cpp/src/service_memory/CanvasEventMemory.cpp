#include "service_memory/CanvasEventMemory.hpp"
#include "service_memory/CanvasServiceMemory.hpp"
#include "service_memory/CanvasItemRules.hpp"
#include <algorithm>
#include <iostream>
#include <unordered_set>

static std::string canvasItemId(const nlohmann::json& value) {
    std::string key;
    if (value.is_string()) key = value.get<std::string>();
    else if (value.is_number_integer()) key = std::to_string(value.get<long long>());
    return key;
}

using namespace agora::canvas;

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

bool CanvasEventMemory::apply(const std::shared_ptr<Canvas>& canvas, const nlohmann::json& event) {
    if (!canvas || !event.is_object()) return false;
    const std::string type = event.value("type", "");
    if (type == "ping" || type == "pong" || type == "item_crdt_change") return true;

    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    const int canvas_id = canvas->getCanvasId();
    if (type == "chat") {
        if (!event.contains("room_id") || !event["room_id"].is_string()
            || !event.contains("sequence") || !event["sequence"].is_number_unsigned()) return false;
        const std::string room_id = event["room_id"].get<std::string>();
        if (room_id.empty()) return false;
        const auto stored_message = storedChatMessage(event);
        if (event.value("room_created", false)) {
            nlohmann::json room = {
                {"type", "chat_room"},
                {"permission", event.value("room_permission", nlohmann::json::array())},
                {"data", nlohmann::json::array({stored_message})},
                {"next_sequence", event["sequence"].get<std::uint64_t>() + 1}
            };
            if (!memory.storeItem(canvas_id, room_id, room)) {
                std::cerr << "[uWebSockets] Failed to create chat room item '" << room_id << "'\n";
                return false;
            }
        } else if (!memory.appendMessage(canvas_id, room_id, event["sequence"].get<std::uint64_t>(), stored_message)) {
            std::cerr << "[uWebSockets] Failed to append chat message to room '" << room_id << "'\n";
            return false;
        }
        return true;
    }
    if (event.contains("items") && event["items"].is_object()) {
        nlohmann::json items = event["items"];
        for (auto& [item_id, item] : items.items()) {
            if (!isChatRoomItem(item) || item.contains("data")) continue;
            auto old_data = memory.readItem(canvas_id, item_id, "data");
            if (old_data) {
                try {
                    const auto result = nlohmann::json::parse(*old_data);
                    if (result.is_array() && !result.empty()) item["data"] = result[0];
                } catch (...) {}
            }
            if (!item.contains("data")) item["data"] = nlohmann::json::array();
            std::uint64_t next_sequence = chatRoomNextSequence(item);
            auto old_next = memory.readItem(canvas_id, item_id, "next_sequence");
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
        return memory.replaceItems(canvas_id, items);
    }

    const nlohmann::json* id = nullptr;
    if (event.contains("item_id")) id = &event["item_id"];
    else if (event.contains("item-id")) id = &event["item-id"];
    if (!id) return false;

    const std::string item_key = canvasItemId(*id);
    if (item_key.empty()) return false;
    if (type == "item_delete" || type == "delete_item") {
        return memory.removeItem(canvas_id, item_key);
    }

    const nlohmann::json* item_payload = nullptr;
    if (event.contains("item") && event["item"].is_object()) item_payload = &event["item"];
    else if (event.contains("data") && event["data"].is_object()) item_payload = &event["data"];
    if (item_payload) {
        nlohmann::json item = *item_payload;
        const std::string item_kind = item.value("kind", "");
        if (item_kind == "text" || item_kind == "note" || item_kind == "code") {
            const auto stored_item = memory.readItem(canvas_id, item_key);
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
            auto old_data = memory.readItem(canvas_id, item_key, "data");
            if (old_data) {
                try {
                    const auto result = nlohmann::json::parse(*old_data);
                    if (result.is_array() && !result.empty()) item["data"] = result[0];
                } catch (...) {}
            }
            if (item.contains("data") && item["data"].is_array()) {
                std::uint64_t next_sequence = chatRoomNextSequence(item);
                auto old_next = memory.readItem(canvas_id, item_key, "next_sequence");
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
        return memory.storeItem(canvas_id, item_key, item);
    }
    return false;
}

