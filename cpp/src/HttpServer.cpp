#include "HttpServer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <limits>
#include <utility>
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
                       const std::string& db_host, int db_port,
                       std::vector<std::unique_ptr<HttpApiModule>> api_modules)
    : canvas_pool_(canvas_pool), api_modules_(std::move(api_modules)), host_(host), port_(port), advertised_host_(advertised_host),
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
    // CORS is transport-wide; endpoint implementations live in API modules.
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

    for (const auto& api_module : api_modules_) {
        if (api_module) api_module->registerRoutes(server_);
    }
}
