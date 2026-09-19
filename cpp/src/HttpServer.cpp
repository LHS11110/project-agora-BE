#include "HttpServer.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include "MssqlClient.hpp"

#include <openssl/sha.h>
#include <jwt-cpp/jwt.h>

HttpServer::HttpServer(CanvasPool& canvas_pool, const std::string& host, int port,
                       const std::string& jwt_secret, const std::string& db_host, int db_port)
    : canvas_pool_(canvas_pool), host_(host), port_(port), jwt_secret_(jwt_secret), db_host_(db_host), db_port_(db_port) {
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



int HttpServer::authenticateTokenForCanvas(const std::string& token, int canvas_id, const std::string& client_ip, int ws_port) {
    if (canvas_id <= 0 || token.empty()) {
        return -1;
    }

    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256(jwt_secret_));
        verifier.verify(decoded);

        if (decoded.has_payload_claim("clientIp")) {
            std::string token_ip = decoded.get_payload_claim("clientIp").as_string();
            if (token_ip != client_ip) {
                std::cerr << "[HttpServer] IP mismatch: token IP (" << token_ip << ") != client IP (" << client_ip << ")\n";
                return -1;
            }
        } else {
            std::cerr << "[HttpServer] JWT missing valid clientIp claim\n";
            return -1;
        }

        if (decoded.has_payload_claim("serverHash")) {
            std::string token_hash = decoded.get_payload_claim("serverHash").as_string();
            
            std::string raw_string = host_ + ":" + std::to_string(ws_port);
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, raw_string.c_str(), raw_string.size());
            SHA256_Final(hash, &sha256);
            
            char hex_string[SHA256_DIGEST_LENGTH * 2 + 1];
            for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                sprintf(&hex_string[i * 2], "%02x", hash[i]);
            }
            std::string generated_hash(hex_string);
            
            if (token_hash != generated_hash) {
                std::cerr << "[HttpServer] Server Hash mismatch: " << token_hash << " != " << generated_hash << "\n";
                return -1;
            }
        } else {
            std::cerr << "[HttpServer] JWT missing valid serverHash claim\n";
            return -1;
        }
        
        std::string nickname = "";
        if (decoded.has_payload_claim("nickname")) {
            nickname = decoded.get_payload_claim("nickname").as_string();
        }
        
        int tag_number = -1;
        if (decoded.has_payload_claim("tagNumber")) {
            tag_number = static_cast<int>(decoded.get_payload_claim("tagNumber").as_integer());
        }

        if (nickname.empty() || tag_number < 0) {
            std::cerr << "[HttpServer] JWT missing valid nickname or tagNumber claim\n";
            return -1;
        }

        MssqlClient mssql(db_host_, db_port_);
        int user_id = mssql.getUserIdAndCheckWithdrawn(nickname, tag_number);
        if (user_id <= 0) {
            std::cerr << "[HttpServer] Rejected connection: User withdrawn or not found (" << nickname << "#" << tag_number << ")\n";
            return -1;
        }

        return user_id;
    } catch (const std::exception& e) {
        std::cerr << "[HttpServer] JWT verification failed: " << e.what() << "\n";
        return -1;
    }
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
