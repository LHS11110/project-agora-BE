#include "HttpServer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <limits>
#include <nlohmann/json.hpp>
#include "MssqlClient.hpp"

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <jwt-cpp/jwt.h>

namespace {
std::string normalizeClientIp(std::string ip) {
    // uWebSockets can present an IPv4 peer as an IPv4-mapped IPv6 address
    // when nginx connects over the local loopback interface.
    constexpr const char* ipv4MappedPrefix = "::ffff:";
    if (ip.rfind(ipv4MappedPrefix, 0) == 0) {
        return ip.substr(std::char_traits<char>::length(ipv4MappedPrefix));
    }
    return ip;
}
}

HttpServer::HttpServer(CanvasPool& canvas_pool, const std::string& host, int port,
                       const std::string& advertised_host, const std::string& jwt_secret,
                       const std::string& db_host, int db_port)
    : canvas_pool_(canvas_pool), host_(host), port_(port), advertised_host_(advertised_host),
      jwt_secret_(jwt_secret), db_host_(db_host), db_port_(db_port) {
    // cpp-httplib enables SO_REUSEPORT by default on Linux. That lets a second
    // server bind the same port and makes the kernel distribute requests to a
    // stale/stopped process. Keep fast restarts via SO_REUSEADDR while ensuring
    // that only one Agora REST server can own the port.
    server_.set_socket_options([](socket_t socket) {
        int enabled = 1;
        ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    });
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



std::optional<AuthenticatedUser> HttpServer::authenticateTokenForCanvas(const std::string& token, int canvas_id, const std::string& client_ip, int ws_port) {
    if (canvas_id <= 0 || token.empty()) {
        return std::nullopt;
    }

    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256(jwt_secret_));
        verifier.verify(decoded);

        if (decoded.get_subject() != "canvas-access" ||
            !decoded.has_payload_claim("canvasId") ||
            decoded.get_payload_claim("canvasId").as_integer() != canvas_id) {
            std::cerr << "[HttpServer] JWT is not authorized for canvas #" << canvas_id << "\n";
            return std::nullopt;
        }

        if (decoded.has_payload_claim("clientIp")) {
            std::string token_ip = decoded.get_payload_claim("clientIp").as_string();
            if (normalizeClientIp(token_ip) != normalizeClientIp(client_ip)) {
                std::cerr << "[HttpServer] IP mismatch: token IP (" << token_ip << ") != client IP (" << client_ip << ")\n";
                return std::nullopt;
            }
        } else {
            std::cerr << "[HttpServer] JWT missing valid clientIp claim\n";
            return std::nullopt;
        }

        if (decoded.has_payload_claim("serverHash")) {
            std::string token_hash = decoded.get_payload_claim("serverHash").as_string();
            
            // Spring signs the address registered in cpp_server.  host_ is only
            // the local bind address (commonly 0.0.0.0), so hashing it rejects
            // every valid token when ADVERTISE_IP is a public/private address.
            std::string raw_string = advertised_host_ + ":" + std::to_string(ws_port);
            unsigned char hash[EVP_MAX_MD_SIZE];
            unsigned int hash_len = 0;
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (ctx != nullptr) {
                EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
                EVP_DigestUpdate(ctx, raw_string.c_str(), raw_string.size());
                EVP_DigestFinal_ex(ctx, hash, &hash_len);
                EVP_MD_CTX_free(ctx);
            }
            
            char hex_string[EVP_MAX_MD_SIZE * 2 + 1];
            for (unsigned int i = 0; i < hash_len; i++) {
                sprintf(&hex_string[i * 2], "%02x", (unsigned int)hash[i]);
            }
            std::string generated_hash(hex_string);
            
            if (token_hash != generated_hash) {
                std::cerr << "[HttpServer] Server Hash mismatch: " << token_hash << " != " << generated_hash << "\n";
                return std::nullopt;
            }
        } else {
            std::cerr << "[HttpServer] JWT missing valid serverHash claim\n";
            return std::nullopt;
        }
        
        if (!decoded.has_payload_claim("nickname") || !decoded.has_payload_claim("tagNumber")) {
            std::cerr << "[HttpServer] JWT missing nickname or tagNumber claim\n";
            return std::nullopt;
        }
        const std::string nickname = decoded.get_payload_claim("nickname").as_string();
        const auto tag_number = decoded.get_payload_claim("tagNumber").as_integer();
        if (!decoded.has_payload_claim("settingsRevision")) {
            std::cerr << "[HttpServer] JWT missing settingsRevision claim\n";
            return std::nullopt;
        }
        const auto settings_revision = decoded.get_payload_claim("settingsRevision").as_integer();
        if (nickname.empty() || tag_number < 0 || tag_number > std::numeric_limits<int>::max()
            || settings_revision < 0) {
            std::cerr << "[HttpServer] JWT has invalid nickname or tagNumber claim\n";
            return std::nullopt;
        }

        MssqlClient mssql(db_host_, db_port_);
        int user_id = mssql.getActiveUserId(nickname, static_cast<int>(tag_number));
        if (user_id <= 0) {
            std::cerr << "[HttpServer] Rejected connection: User inactive, not found, or database unavailable (" << nickname << "#" << tag_number << ")\n";
            return std::nullopt;
        }

        return AuthenticatedUser{user_id, static_cast<int>(tag_number), nickname, settings_revision};
    } catch (const std::exception& e) {
        std::cerr << "[HttpServer] JWT verification failed: " << e.what() << "\n";
        return std::nullopt;
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
        (void)req;
        int count = canvas_pool_.getActiveCanvasCount();
        nlohmann::json r = {{"status", "success"}, {"count", count}};
        res.status = 200;
        res.set_content(r.dump(), "application/json");
    });

    // 활성 캔버스 상세 목록 및 수 조회 API
    server_.Get("/api/canvas/active", [this](const httplib::Request& req, httplib::Response& res) {
        (void)req;
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
        (void)req;
        res.status = 200;
        res.set_content("{\"status\":\"UP\",\"service\":\"Agora C++ Realtime Server\"}", "application/json");
    });

    server_.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        res.status = 200;
        res.set_content("{\"status\":\"online\",\"service\":\"Agora C++ Server\"}", "application/json");
    });
}
