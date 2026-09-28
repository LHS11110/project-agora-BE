#include "CanvasControlApi.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <charconv>
#include <system_error>

namespace {
bool parsePositiveId(const std::string& value, int& result) {
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    return error == std::errc{} && end == value.data() + value.size() && result > 0;
}

void writeInvalidId(httplib::Response& res) {
    res.status = 400;
    res.set_content(R"({"status":400,"error":"BAD_REQUEST","message":"잘못된 ID입니다."})",
                    "application/json; charset=utf-8");
}
}

void CanvasControlApi::registerRoutes(httplib::Server& server) {
    // These routes control in-memory realtime state owned by CanvasPool.
    server.Post(R"(/api/users/(\d+)/disconnect)", [this](const httplib::Request& req, httplib::Response& res) {
        int user_id = 0;
        if (!parsePositiveId(req.matches[1], user_id)) return writeInvalidId(res);
        canvas_pool_.disconnectUserFromAll(user_id);
        res.status = 200;
        res.set_content(R"({"status":"success","message":"User disconnected"})", "application/json");
    });

    server.Post(R"(/api/canvas/(\d+)/users/(\d+)/disconnect)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = 0;
        int user_id = 0;
        if (!parsePositiveId(req.matches[1], canvas_id) || !parsePositiveId(req.matches[2], user_id)) {
            return writeInvalidId(res);
        }
        canvas_pool_.disconnectUser(canvas_id, user_id);
        res.status = 200;
        res.set_content(R"({"status":"success","message":"User disconnected from canvas"})", "application/json");
    });

    server.Delete(R"(/api/canvas/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = 0;
        if (!parsePositiveId(req.matches[1], canvas_id)) return writeInvalidId(res);
        const bool removed = canvas_pool_.removeCanvas(canvas_id);
        const nlohmann::json body = {
            {"status", "success"}, {"canvas_id", canvas_id}, {"removed", removed}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/api/canvas/count", [this](const httplib::Request&, httplib::Response& res) {
        const nlohmann::json body = {
            {"status", "success"}, {"count", canvas_pool_.getActiveCanvasCount()}
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/api/canvas/active", [this](const httplib::Request&, httplib::Response& res) {
        const auto ids = canvas_pool_.getActiveCanvasIds();
        nlohmann::json canvases = nlohmann::json::array();
        for (const int canvas_id : ids) {
            const auto canvas = canvas_pool_.getCanvas(canvas_id);
            if (!canvas) continue;
            canvases.push_back({
                {"canvas_id", canvas_id},
                {"canvas_name", canvas->getCanvasName()},
                {"admin_user_id", canvas->getAdminUserId()},
                {"active_user_count", canvas->getActiveUsers().size()},
                {"active_users", canvas->getActiveUsers()}
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
