#include "HttpServer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <charconv>
#include <array>
#include <limits>
#include <utility>
#include <nlohmann/json.hpp>
#include "MssqlClient.hpp"
#include "Environment.hpp"

#include <openssl/crypto.h>
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
    internal_api_token_ = environmentValue("CPP_INTERNAL_API_TOKEN");
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
        int parsed_size = 0;
        const std::string configured(env_pool);
        const auto [end, error] = std::from_chars(configured.data(), configured.data() + configured.size(), parsed_size);
        if (error == std::errc{} && end == configured.data() + configured.size()
                && parsed_size >= 1 && parsed_size <= 128) {
            thread_pool_size = parsed_size;
        } else {
            std::cerr << "[HttpServer] Invalid REST_THREAD_POOL; using 16 threads.\n";
        }
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
            std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
            unsigned int hash_len = 0;
            if (EVP_Digest(raw_string.data(), raw_string.size(), hash.data(), &hash_len,
                           EVP_sha256(), nullptr) != 1 || hash_len != hash.size()) {
                std::cerr << "[HttpServer] Could not calculate the registered server hash\n";
                return std::nullopt;
            }
            std::ostringstream hash_stream;
            hash_stream << std::hex << std::setfill('0');
            for (unsigned int i = 0; i < hash_len; ++i) {
                hash_stream << std::setw(2) << static_cast<unsigned int>(hash[i]);
            }
            const std::string generated_hash = hash_stream.str();
            
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
    server_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        const bool api_path = req.path == "/api" || req.path.rfind("/api/", 0) == 0;
        if (!api_path || req.method == "OPTIONS") return httplib::Server::HandlerResponse::Unhandled;

        const std::string supplied = req.get_header_value("X-Agora-Internal-Token");
        const bool valid_token = internal_api_token_.size() >= 32
                && supplied.size() == internal_api_token_.size()
                && CRYPTO_memcmp(supplied.data(), internal_api_token_.data(), supplied.size()) == 0;
        if (!valid_token) {
            res.status = 401;
            res.set_content(R"({"status":401,"error":"UNAUTHORIZED","message":"인증이 필요합니다."})",
                            "application/json; charset=utf-8");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

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
