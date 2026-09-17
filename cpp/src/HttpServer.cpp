#include "HttpServer.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

HttpServer::HttpServer(CanvasPool& canvas_pool, const std::string& host, int port)
    : canvas_pool_(canvas_pool), host_(host), port_(port) {
    setupRoutes();
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    int thread_pool_size = 16;
    if (const char* env_pool = std::getenv("REST_THREAD_POOL")) {
        thread_pool_size = std::stoi(env_pool);
    }
    std::cout << "[HttpServer] Configuring REST Thread Pool with " << thread_pool_size << " threads.\n";
    server_.new_task_queue = [thread_pool_size] { return new httplib::ThreadPool(thread_pool_size); };

    std::cout << "[HttpServer] Starting Agora C++ Server on " << host_ << ":" << port_ << "...\n";
    server_.listen(host_.c_str(), port_);
}

void HttpServer::stop() {
    server_.stop();
}

bool HttpServer::registerToken(int user_id, const std::string& token, int canvas_id) {
    if (user_id <= 0 || token.empty() || canvas_id <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(auth_mutex_);
    auto [it, inserted] = token_to_user_.try_emplace(token);
    if (!inserted && it->second.user_id != user_id) {
        std::cerr << "[HttpServer] Rejected JWT token registration for conflicting user #" << user_id << "\n";
        return false;
    }

    it->second.user_id = user_id;
    it->second.canvas_ids.insert(canvas_id);
    std::cout << "[HttpServer] Registered JWT token for user #" << user_id
              << " on Canvas #" << canvas_id << "\n";
    return true;
}

int HttpServer::authenticateToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auto it = token_to_user_.find(token);
    if (it != token_to_user_.end()) {
        return it->second.user_id;
    }
    return -1;
}

int HttpServer::authenticateTokenForCanvas(const std::string& token, int canvas_id) {
    if (canvas_id <= 0) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(auth_mutex_);
    auto it = token_to_user_.find(token);
    if (it != token_to_user_.end() && it->second.canvas_ids.count(canvas_id) > 0) {
        return it->second.user_id;
    }
    return -1;
}

void HttpServer::setupRoutes() {
    // Global CORS Preflight and headers
    server_.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "*");
        res.status = 204;
    });

    server_.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "*");
    });

    // 1. JWT 토큰 등록 API (POST /api/auth/token)
    server_.Post("/api/auth/token", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            int user_id = body.value("user_id", -1);
            std::string token = body.value("token", "");
            int canvas_id = body.value("canvas_id", 0);
            if (canvas_id == 0) canvas_id = body.value("canvasId", 0);

            if (user_id <= 0 || token.empty() || canvas_id <= 0) {
                res.status = 400;
                res.set_content("{\"error\":\"user_id, token, and canvas_id are required\"}", "application/json");
                return;
            }

            if (!registerToken(user_id, token, canvas_id)) {
                res.status = 409;
                res.set_content("{\"error\":\"Token is already registered to another user\"}", "application/json");
                return;
            }

            // C++ 메모리 풀에 캔버스 사전 로드 및 활성화
            canvas_pool_.getOrCreateCanvas(canvas_id);
            std::cout << "[HttpServer] Canvas #" << canvas_id << " loaded & activated in pool on token register\n";

            res.status = 200;
            res.set_content("{\"status\":\"success\",\"message\":\"Token registered\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // POST /api/access removed as per user request (봇용 API 제거)



    // 사용자 연결 중단 API (Spring 회원 삭제 시 호출)
    server_.Post(R"(/api/users/(\d+)/disconnect)", [this](const httplib::Request& req, httplib::Response& res) {
        int user_id = std::stoi(req.matches[1]);
        canvas_pool_.disconnectUserFromAll(user_id);
        res.status = 200;
        res.set_content("{\"status\":\"success\",\"message\":\"User disconnected\"}", "application/json");
    });

    // 캔버스별 사용자 연결 중단 API
    server_.Post(R"(/api/canvas/(\d+)/users/(\d+)/disconnect)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        int user_id = std::stoi(req.matches[2]);
        canvas_pool_.disconnectUser(canvas_id, user_id);
        res.status = 200;
        res.set_content("{\"status\":\"success\",\"message\":\"User disconnected from canvas\"}", "application/json");
    });

    // POST /api/access/disconnect removed as per user request (봇용 API 제거)

    // 캔버스 제거 API (Spring 캔버스 삭제 시 호출)
    server_.Delete(R"(/api/canvas/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        bool removed = canvas_pool_.removeCanvas(canvas_id);
        res.status = 200;
        nlohmann::json r = {{"status", "success"}, {"canvas_id", canvas_id}, {"removed", removed}};
        res.set_content(r.dump(), "application/json");
    });

    // 활성 캔버스 수 조회 API (로드 밸런서 측정용)
    server_.Get("/api/canvas/count", [this](const httplib::Request& req, httplib::Response& res) {
        int count = canvas_pool_.getActiveCanvasCount();
        nlohmann::json r = {{"status", "success"}, {"count", count}};
        res.status = 200;
        res.set_content(r.dump(), "application/json");
    });

    // 활성 캔버스 상세 목록 및 수 조회 API
    server_.Get("/api/canvas/active", [this](const httplib::Request& req, httplib::Response& res) {
        auto ids = canvas_pool_.getActiveCanvasIds();
        nlohmann::json canvas_list = nlohmann::json::array();
        for (int cid : ids) {
            auto c = canvas_pool_.getCanvas(cid);
            if (c) {
                canvas_list.push_back({
                    {"canvas_id", cid},
                    {"canvas_name", c->getCanvasName()},
                    {"admin_user_id", c->getAdminUserId()},
                    {"active_user_count", c->getActiveUsers().size()},
                    {"active_users", c->getActiveUsers()}
                });
            }
        }
        nlohmann::json r = {
            {"status", "success"},
            {"count", (int)ids.size()},
            {"canvases", canvas_list}
        };
        res.status = 200;
        res.set_content(r.dump(), "application/json");
    });

    // 헬스체크
    server_.Get("/health", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"status\":\"UP\",\"service\":\"Agora C++ Realtime Server\"}", "application/json");
    });

    server_.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"status\":\"online\",\"service\":\"Agora C++ Server\"}", "application/json");
    });
}
