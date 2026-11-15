#include "service/CanvasControlService.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <charconv>
#include <system_error>

namespace {
void writeInvalidId(HttpApi::Response& res) {
    res.status = 400;
    res.set_content(R"({"status":400,"error":"BAD_REQUEST","message":"잘못된 ID입니다."})",
                    "application/json; charset=utf-8");
}

void registerScopedControlRoutes(HttpApi& api, CanvasLifecycleMemory& canvas_pool,
                               SpaceScope scope, const std::string& prefix) {
    const std::string id_key = scope == SpaceScope::Workspace ? "workspace_id" : "canvas_id";
    const std::string name_key = scope == SpaceScope::Workspace ? "workspace_name" : "canvas_name";
    const std::string list_key = scope == SpaceScope::Workspace ? "workspaces" : "canvases";

    api.remove(prefix + R"(/(\d+))", [&canvas_pool, scope, id_key](const HttpApi::Request& req, HttpApi::Response& res) {
        int space_id = 0;
        if (!HttpApi::positiveId(req.matches[1], space_id)) return writeInvalidId(res);
        const bool removed = canvas_pool.removeCanvas(scope, space_id);
        const nlohmann::json body = {
            {"status", "success"},
            {id_key, space_id},
            {"scope", std::string(spaceScopeWire(scope))},
            {"removed", removed}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    api.get(prefix + "/count", [&canvas_pool](const HttpApi::Request&, HttpApi::Response& res) {
        const nlohmann::json body = {
            {"status", "success"}, {"count", canvas_pool.getActiveCanvasCount()}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    api.get(prefix + "/active", [&canvas_pool, scope, id_key, name_key, list_key](const HttpApi::Request&, HttpApi::Response& res) {
        const auto keys = canvas_pool.getActiveSpaceKeys();
        nlohmann::json entries = nlohmann::json::array();
        for (const auto& key : keys) {
            if (key.scope != scope) continue;
            const auto space = canvas_pool.getCanvas(scope, key.id);
            if (!space) continue;
            entries.push_back({
                {id_key, key.id},
                {name_key, space->getCanvasName()},
                {"admin_user_id", space->getAdminUserId()}
            });
        }

        const nlohmann::json body = {
            {"status", "success"},
            {"count", entries.size()},
            {list_key, std::move(entries)}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    api.post(prefix + R"(/(\d+)/workspace-sync)", [&canvas_pool, scope, id_key](const HttpApi::Request& req, HttpApi::Response& res) {
        int space_id = 0;
        if (!HttpApi::positiveId(req.matches[1], space_id)) return writeInvalidId(res);
        const auto space = canvas_pool.getCanvas(scope, space_id);
        const nlohmann::json body = {
            {"status", "success"},
            {id_key, space_id},
            {"scope", std::string(spaceScopeWire(scope))},
            {"canvas_active_in_cpp", static_cast<bool>(space)}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });
}
}

void CanvasControlService::registerRoutes(HttpApi& api) {
    registerScopedControlRoutes(api, canvas_pool_, SpaceScope::Canvas, "/api/canvas");
    registerScopedControlRoutes(api, canvas_pool_, SpaceScope::Workspace, "/api/workspace");
}
