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
}

void CanvasControlService::registerRoutes(HttpApi& api) {
    api.remove(R"(/api/canvas/(\d+))", [this](const HttpApi::Request& req, HttpApi::Response& res) {
        int canvas_id = 0;
        if (!HttpApi::positiveId(req.matches[1], canvas_id)) return writeInvalidId(res);
        const bool removed = canvas_pool_.removeCanvas(canvas_id);
        const nlohmann::json body = {
            {"status", "success"}, {"canvas_id", canvas_id}, {"removed", removed}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    api.get("/api/canvas/count", [this](const HttpApi::Request&, HttpApi::Response& res) {
        const nlohmann::json body = {
            {"status", "success"}, {"count", canvas_pool_.getActiveCanvasCount()}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    api.get("/api/canvas/active", [this](const HttpApi::Request&, HttpApi::Response& res) {
        const auto ids = canvas_pool_.getActiveCanvasIds();
        nlohmann::json canvases = nlohmann::json::array();
        for (const int canvas_id : ids) {
            const auto canvas = canvas_pool_.getCanvas(canvas_id);
            if (!canvas) continue;
            canvases.push_back({
                {"canvas_id", canvas_id},
                {"canvas_name", canvas->getCanvasName()},
                {"admin_user_id", canvas->getAdminUserId()}
            });
        }

        const nlohmann::json body = {
            {"status", "success"},
            {"count", ids.size()},
            {"canvases", std::move(canvases)}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });
}
