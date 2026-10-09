#include "service/CanvasSettingsService.hpp"
#include "service/CanvasAccessRules.hpp"
#include "service_memory/CanvasServiceMemory.hpp"
#include "service_memory/RegistryServiceMemory.hpp"
#include "service_memory/CanvasSnapshotMemory.hpp"
#include "CanvasPassword.hpp"
#include <algorithm>
#include <map>
#include <iostream>

static bool isCanvasParticipant(const nlohmann::json& doc, int user_id) {
    if (!doc.contains("people") || !doc["people"].is_array()) return false;
    for (const auto& member : doc["people"]) {
        if (member.is_number_integer() && member.get<int>() == user_id) return true;
    }
    return false;
}

static nlohmann::json canvasSettingsSnapshot(const nlohmann::json& doc, RegistryServiceMemory& db) {
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

CanvasSettingsTaskResult CanvasSettingsService::execute(
        CanvasLifecycleMemory& pool, const std::shared_ptr<Canvas>& canvas, int canvas_id,
        int user_id, const std::string& nickname, int tag_number,
        const nlohmann::json& event, const std::string& request_id) {
    CanvasSettingsTaskResult result;
    auto reject = [&result, &request_id](const char* code) {
        result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
                                         {"request_id", request_id}, {"code", code}};
        return result;
    };
    if (!canvas) return reject("CANVAS_NOT_READY");

    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    auto raw = memory.loadCanvas(canvas_id);
    if (!raw) return reject("SETTINGS_STORAGE_ERROR");
    nlohmann::json doc;
    try { doc = nlohmann::json::parse(*raw); } catch (...) { return reject("SETTINGS_STORAGE_ERROR"); }
    if (!doc.is_object()) return reject("SETTINGS_STORAGE_ERROR");

    RegistryServiceMemory db(pool.getDbHost(), pool.getDbPort());
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
    std::map<std::string, nlohmann::json> original_settings;
    auto rememberOriginal = [&](const std::string& document_field) {
        if (doc.contains(document_field)) original_settings.emplace(document_field, doc[document_field]);
    };
    auto addChange = [&](const std::string& document_field,
                         const nlohmann::json& value) {
        changes.emplace_back(document_field, value);
        es_changes[document_field] = value;
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
            addChange("canvas-name", value);
        } else if (field == "description") {
            rememberOriginal("description");
            doc["description"] = value;
            addChange("description", value);
        } else {
            rememberOriginal("canvas-password-hash");
            nlohmann::json hash = nullptr;
            if (value.find_first_not_of(" \t\n\r\f\v") != std::string::npos) {
                auto generated = hashCanvasPassword(value);
                if (!generated) return reject("SETTINGS_STORAGE_ERROR");
                hash = *generated;
            }
            doc["canvas-password-hash"] = hash;
            addChange("canvas-password-hash", hash);
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
        addChange("people", doc["people"]);
        addChange("inner-group", doc["inner-group"]);
    } else {
        return reject("SETTINGS_INVALID_INPUT");
    }

    doc["settings-revision"] = revision + 1;
    addChange("settings-revision", revision + 1);
    const auto stored = memory.compareAndSetFields(canvas_id, revision, changes);
    if (stored == CanvasServiceMemory::CompareSetResult::Conflict) {
        auto latest_raw = memory.loadCanvas(canvas_id);
        nlohmann::json latest = doc;
        if (latest_raw) {
            try { latest = nlohmann::json::parse(*latest_raw); } catch (...) {}
        }
        result.response = nlohmann::json{{"type", "canvas_settings_result"}, {"ok", false},
            {"request_id", request_id}, {"code", "SETTINGS_CONFLICT"},
            {"settings", canvasSettingsSnapshot(latest, db)}};
        return result;
    }
    if (stored != CanvasServiceMemory::CompareSetResult::Applied) return reject("SETTINGS_STORAGE_ERROR");

    CanvasSnapshotMemory es(pool.getEsHost(), pool.getEsPort());
    if (!es.patchCanvasFields(canvas_id, es_changes)) {
        std::vector<std::pair<std::string, nlohmann::json>> rollback;
        std::map<std::string, nlohmann::json> restore_in_es;
        for (const auto& [document_field, updated_value] : es_changes) {
            (void)updated_value;
            const nlohmann::json original_value = document_field == "settings-revision"
                ? nlohmann::json(revision)
                : (original_settings.find(document_field) != original_settings.end()
                    ? original_settings.at(document_field) : nlohmann::json(nullptr));
            rollback.emplace_back(document_field, original_value);
            restore_in_es[document_field] = original_value;
        }
        if (memory.compareAndSetFields(canvas_id, revision + 1, rollback) != CanvasServiceMemory::CompareSetResult::Applied) {
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

