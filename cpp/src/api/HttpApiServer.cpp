#include "api/HttpApiServer.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <charconv>
#include <array>
#include <limits>
#include <utility>
#include <nlohmann/json.hpp>
#include "Environment.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include "RequestLogContext.hpp"

#include <openssl/ssl.h>
#include <stdexcept>
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

HttpApiServer::HttpApiServer(const std::string& host, int port,
                       std::vector<std::unique_ptr<HttpServiceModule>> services)
    : api_modules_(std::move(services)), host_(host), port_(port),
      server_(environmentValue("SERVICE_TLS_CERT").c_str(), environmentValue("SERVICE_TLS_KEY").c_str()) {
    if (!server_.is_valid()) throw std::runtime_error("HTTPS requires valid SERVICE_TLS_CERT and SERVICE_TLS_KEY");
    if (SSL_CTX_set_min_proto_version(server_.ssl_context(), TLS1_3_VERSION) != 1)
        throw std::runtime_error("Cannot enforce TLS 1.3 for HTTPS server");
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

HttpApiServer::~HttpApiServer() {
    stop();
}

void HttpApiServer::start() {
    int thread_pool_size = 16;
    if (const char* env_pool = std::getenv("REST_THREAD_POOL")) {
        int parsed_size = 0;
        const std::string configured(env_pool);
        const auto [end, error] = std::from_chars(configured.data(), configured.data() + configured.size(), parsed_size);
        if (error == std::errc{} && end == configured.data() + configured.size()
                && parsed_size >= 1 && parsed_size <= 128) {
            thread_pool_size = parsed_size;
        } else {
            std::cerr << "[HttpApiServer] Invalid REST_THREAD_POOL; using 16 threads.\n";
        }
    }
    std::cout << "[HttpApiServer] Configuring REST Thread Pool with " << thread_pool_size << " threads.\n";
    server_.new_task_queue = [thread_pool_size] { return new httplib::ThreadPool(thread_pool_size); };

    std::cout << "[HttpApiServer] Starting Agora C++ Server on " << host_ << ":" << port_ << "...\n";
    server_.listen(host_.c_str(), port_);
}

void HttpApiServer::stop() {
    server_.stop();
}



void HttpApiServer::setupRoutes() {
    server_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        const std::string request_id = agora::logging::acceptedRequestId(req.get_header_value("X-Request-ID"));
        const std::string operation = req.method + " " + req.path;
        agora::logging::setCurrentRequestLogContext({
            request_id, operation, "in_progress", {}, std::chrono::steady_clock::now(), {}
        });
        res.set_header("X-Request-ID", request_id);

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

    server_.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                     std::exception_ptr exception) {
        std::string error_type = "unknown";
        std::string error_message = "REST handler raised an exception";
        try {
            if (exception) std::rethrow_exception(exception);
        } catch (const std::exception& error) {
            error_type = typeid(error).name();
            error_message = error.what();
        } catch (...) {
            error_type = "unknown";
        }
        if (error_message.size() > 500) error_message.resize(500);
        res.status = 500;
        res.set_content(R"({"status":500,"error":"INTERNAL_SERVER_ERROR"})",
                        "application/json; charset=utf-8");
        agora::logging::markCurrentRequestFailure("HTTP_500");
        ElasticsearchBulkLogBuffer::instance().record(
            "http", "http_request_exception", "ERROR", "REST handler raised an exception",
            {{"error_type", error_type}, {"error_message", error_message}}, "failure", "HTTP_500");
    });

    server_.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status != 404) return;
        nlohmann::json body = {
            {"status", 404}, {"error", "NOT_FOUND"},
            {"message", "요청한 경로를 찾을 수 없습니다."}
        };
        const std::string& request_id = agora::logging::currentRequestLogContext().request_id;
        if (!request_id.empty()) body["request_id"] = request_id;
        res.set_content(body.dump(), "application/json; charset=utf-8");
    });

    server_.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        const int status = res.status > 0 ? res.status : 500;
        const std::string outcome = status >= 500 ? "failure" : status >= 400 ? "rejected" : "success";
        const std::string level = status >= 500 ? "ERROR" : status >= 400 ? "WARN" : "INFO";
        std::string error_code = status >= 400 ? "HTTP_" + std::to_string(status) : "";
        if (status >= 400 && res.body.size() <= 16 * 1024) {
            try {
                const auto body = nlohmann::json::parse(res.body);
                if (body.is_object()) {
                    for (const char* field : {"error_code", "code", "error"}) {
                        if (!body.contains(field) || !body[field].is_string()) continue;
                        const std::string candidate = body[field].get<std::string>();
                        const bool safe_code = !candidate.empty() && candidate.size() <= 64
                            && std::all_of(candidate.begin(), candidate.end(), [](unsigned char character) {
                                return std::isalnum(character) || character == '_' || character == '.'
                                    || character == ':' || character == '-';
                            });
                        if (safe_code) {
                            error_code = candidate;
                            break;
                        }
                    }
                }
            } catch (...) {
            }
        }
        const auto& context = agora::logging::currentRequestLogContext();
        const auto duration_ms = context.started_at.time_since_epoch().count() == 0
            ? 0LL
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - context.started_at).count();
        ElasticsearchBulkLogBuffer::instance().record(
            "http", "http_request", level,
            status >= 400 ? "REST request completed with failure" : "REST request completed",
            {{"http_method", req.method}, {"path", req.path}, {"http_status", status},
             {"duration_ms", duration_ms}}, outcome, error_code);
        agora::logging::clearCurrentRequestLogContext();
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

    HttpApi api(server_);
    for (const auto& api_module : api_modules_) {
        if (api_module) api_module->registerRoutes(api);
    }
}
