#include "memory/RedisClient.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include "Environment.hpp"
#include "memory/StorageEndpointRoutes.hpp"
#include <iostream>
#include <sstream>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#include <netdb.h>
#include <thread>
#include <chrono>
#include <climits>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <openssl/err.h>
#include <openssl/x509v3.h>

namespace {
constexpr std::size_t kMaximumRedisBulkReplyBytes = 256 * 1024 * 1024;
constexpr std::size_t kMaximumRedisArrayElements = 1'000'000;
constexpr char kRedisErrorMarker = '\x01';
std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    return environmentValue(name);
}

bool envEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) return true;
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized == "true" || normalized == "1" || normalized == "yes";
}

bool parseRespLength(const std::string& text, long long& value) {
    if (text.empty()) return false;
    try {
        std::size_t parsed_length = 0;
        value = std::stoll(text, &parsed_length);
        return parsed_length == text.size();
    } catch (...) {
        return false;
    }
}

std::vector<std::pair<std::string, int>> parseSentinelSeeds(const char* configured) {
    std::vector<std::pair<std::string, int>> seeds;
    if (!configured || !*configured) return seeds;

    std::istringstream input(configured);
    std::string entry;
    while (std::getline(input, entry, ',')) {
        const auto first = entry.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const auto last = entry.find_last_not_of(" \t\r\n");
        entry = entry.substr(first, last - first + 1);

        std::string host;
        std::string port_text;
        if (!entry.empty() && entry.front() == '[') {
            const auto close = entry.find(']');
            if (close == std::string::npos || close + 1 >= entry.size() || entry[close + 1] != ':') continue;
            host = entry.substr(1, close - 1);
            port_text = entry.substr(close + 2);
        } else {
            const auto separator = entry.rfind(':');
            if (separator == std::string::npos) continue;
            host = entry.substr(0, separator);
            port_text = entry.substr(separator + 1);
        }

        try {
            const int port = std::stoi(port_text);
            if (!host.empty() && port > 0 && port <= 65535) seeds.emplace_back(host, port);
        } catch (...) {
            continue;
        }
    }
    return seeds;
}
}

RedisClient::RedisClient(const std::string& /*logical_host*/, int /*logical_port*/,
                         const std::string& user, const std::string& password)
    : user_(envOr("REDIS_USER", user)),
      password_(envOr("REDIS_USER_PASSWORD", password)),
      sentinel_master_name_(envOr("REDIS_SENTINEL_MASTER_NAME", "agora-master")),
      sentinel_user_(envOr("REDIS_SENTINEL_USER", "")),
      sentinel_password_(envOr("REDIS_SENTINEL_PASSWORD", "")),
      sentinel_seeds_(parseSentinelSeeds(std::getenv("REDIS_SENTINELS"))),
      tls_ca_cert_(envOr("REDIS_TLS_CA_CERT", "")),
      tls_enabled_(envEnabled("REDIS_TLS_ENABLED")), tls_config_valid_(!tls_enabled_),
      socket_fd_(-1), ssl_context_(nullptr), ssl_(nullptr) {
    if (!tls_enabled_) return;
    if (tls_ca_cert_.empty()) {
        std::cerr << "[RedisClient] REDIS_TLS_ENABLED requires REDIS_TLS_CA_CERT\n";
        return;
    }

    ssl_context_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_context_) {
        std::cerr << "[RedisClient] Could not create a Redis TLS client context\n";
        return;
    }
    if (SSL_CTX_set_min_proto_version(ssl_context_, TLS1_3_VERSION) != 1) {
        SSL_CTX_free(ssl_context_);
        ssl_context_ = nullptr;
        std::cerr << "[RedisClient] Cannot enforce TLS 1.3\n";
        return;
    }
    SSL_CTX_set_verify(ssl_context_, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_load_verify_locations(ssl_context_, tls_ca_cert_.c_str(), nullptr) != 1) {
        std::cerr << "[RedisClient] Could not load REDIS_TLS_CA_CERT\n";
        SSL_CTX_free(ssl_context_);
        ssl_context_ = nullptr;
        return;
    }
    tls_config_valid_ = true;
}

RedisClient::~RedisClient() {
    disconnect();
    if (ssl_context_) SSL_CTX_free(ssl_context_);
}

bool RedisClient::connect() {
    if (socket_fd_ >= 0) {
        return true;
    }

    if (password_.empty()) {
        std::cerr << "[RedisClient] REDIS_USER_PASSWORD is not configured\n";
        return false;
    }
    if (tls_enabled_ && !tls_config_valid_) {
        std::cerr << "[RedisClient] Redis TLS is enabled but its CA configuration is invalid\n";
        return false;
    }
    if (sentinel_seeds_.empty()) {
        std::cerr << "[RedisClient] REDIS_SENTINELS is required; direct Redis connections are disabled\n";
        return false;
    }
    if (sentinel_user_.empty() || sentinel_password_.empty()) {
        std::cerr << "[RedisClient] REDIS_SENTINEL_USER and REDIS_SENTINEL_PASSWORD are required\n";
        return false;
    }

    const auto authenticate = [this]() {
        if (user_.empty()) {
            if (!sendCommand({"AUTH", password_})) return false;
        } else if (!sendCommand({"AUTH", user_, password_})) {
            return false;
        }
        const std::string response = readResponse();
        if (response != "OK") {
            std::cerr << "[RedisClient] Redis authentication failed\n";
            disconnect();
            return false;
        }
        return true;
    };

    for (int attempt = 0; attempt < 3; ++attempt) {
        for (const auto& seed : sentinel_seeds_) {
            disconnect();
            if (!connectTo(seed.first, seed.second, 700)) continue;
            const auto auth = std::vector<std::string>{"AUTH", sentinel_user_, sentinel_password_};
            if (!sendCommand(auth) || readResponse() != "OK") {
                disconnect();
                continue;
            }
            if (!sendCommand({"SENTINEL", "get-master-addr-by-name", sentinel_master_name_})) {
                disconnect();
                continue;
            }
            const std::string master_reply = readResponse();
            disconnect();

            std::istringstream master_parts(master_reply);
            std::string master_host;
            int master_port = 0;
            if (!(master_parts >> master_host >> master_port) || master_host.empty() || master_port <= 0 || master_port > 65535) {
                continue;
            }
            if (!connectTo(master_host, master_port, 1200) || !authenticate()) continue;

            if (!sendCommand({"ROLE"})) continue;
            std::istringstream role_parts(readResponse());
            std::string role;
            role_parts >> role;
            if (role == "master") {
                const std::string endpoint = master_host + ":" + std::to_string(master_port);
                ElasticsearchBulkLogBuffer::instance().reportAvailability(
                        "redis-sentinel", true, {{"endpoint", endpoint}});
                ElasticsearchBulkLogBuffer::instance().reportPrimaryChange("redis-sentinel", endpoint);
                return true;
            }

            // Sentinel can briefly return its previous view during promotion.
            // Verify the role on the data node before accepting the connection.
            disconnect();
        }
        if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
    }
    ElasticsearchBulkLogBuffer::instance().reportAvailability(
            "redis-sentinel", false, {{"seed_count", sentinel_seeds_.size()}});
    std::cerr << "[RedisClient] Could not discover a writable Redis primary from Sentinel\n";
    disconnect();
    return false;
}

bool RedisClient::connectTo(const std::string& host, int port, int timeout_ms) {
    if (socket_fd_ >= 0) disconnect();

    const auto dial = resolveStorageDialAddress(environmentValue("REDIS_BROKER_ROUTES"), host, port);
    if (!dial) return false;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(dial->port);
    if (getaddrinfo(dial->host.c_str(), service.c_str(), &hints, &addresses) != 0) return false;

    bool connected = false;
    for (addrinfo* address = addresses; address && !connected; address = address->ai_next) {
        int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv{};
        tv.tv_sec = 2;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

        const int original_flags = fcntl(fd, F_GETFL, 0);
        if (original_flags >= 0) fcntl(fd, F_SETFL, original_flags | O_NONBLOCK);
        int result = ::connect(fd, address->ai_addr, address->ai_addrlen);
        if (result < 0 && errno == EINPROGRESS) {
            pollfd pfd{fd, POLLOUT, 0};
            result = poll(&pfd, 1, timeout_ms);
            if (result > 0) {
                int socket_error = 0;
                socklen_t error_length = sizeof(socket_error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) == 0 && socket_error == 0) {
                    result = 0;
                } else {
                    result = -1;
                }
            } else {
                result = -1;
            }
        }
        if (original_flags >= 0) fcntl(fd, F_SETFL, original_flags);
        if (result == 0) {
            SSL* candidate_ssl = nullptr;
            if (tls_enabled_) {
                candidate_ssl = SSL_new(ssl_context_);
                if (!candidate_ssl || SSL_set_fd(candidate_ssl, fd) != 1) {
                    if (candidate_ssl) SSL_free(candidate_ssl);
                    close(fd);
                    continue;
                }

                in_addr ipv4{};
                in6_addr ipv6{};
                X509_VERIFY_PARAM* verify = SSL_get0_param(candidate_ssl);
                const bool is_ip = inet_pton(AF_INET, host.c_str(), &ipv4) == 1
                        || inet_pton(AF_INET6, host.c_str(), &ipv6) == 1;
                const bool identity_set = is_ip
                        ? X509_VERIFY_PARAM_set1_ip_asc(verify, host.c_str()) == 1
                        : X509_VERIFY_PARAM_set1_host(verify, host.c_str(), 0) == 1;
                if ((!is_ip && SSL_set_tlsext_host_name(candidate_ssl, host.c_str()) != 1)
                        || !identity_set || SSL_connect(candidate_ssl) != 1
                        || SSL_get_verify_result(candidate_ssl) != X509_V_OK) {
                    SSL_free(candidate_ssl);
                    close(fd);
                    ERR_clear_error();
                    continue;
                }
            }
            socket_fd_ = fd;
            ssl_ = candidate_ssl;
            connected = true;
        } else {
            close(fd);
        }
    }
    freeaddrinfo(addresses);
    return connected;
}

void RedisClient::disconnect() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

ssize_t RedisClient::readTransport(void* buffer, std::size_t size) {
    if (ssl_) return SSL_read(ssl_, buffer, static_cast<int>(std::min(size, static_cast<std::size_t>(INT_MAX))));
    return ::read(socket_fd_, buffer, size);
}

ssize_t RedisClient::writeTransport(const void* buffer, std::size_t size) {
    if (ssl_) return SSL_write(ssl_, buffer, static_cast<int>(std::min(size, static_cast<std::size_t>(INT_MAX))));
    return ::write(socket_fd_, buffer, size);
}

bool RedisClient::sendCommand(const std::vector<std::string>& args) {
    if (socket_fd_ < 0 && !connect()) {
        return false;
    }

    std::string msg;
    msg.push_back('*');
    msg.append(std::to_string(args.size()));
    msg.append("\r\n");
    for (const auto& arg : args) {
        msg.push_back('$');
        msg.append(std::to_string(arg.size()));
        msg.append("\r\n");
        msg.append(arg);
        msg.append("\r\n");
    }

    std::size_t total = 0;
    while (total < msg.size()) {
        ssize_t sent = writeTransport(msg.data() + total, msg.size() - total);
        if (sent <= 0) {
            disconnect();
            return false;
        }
        total += static_cast<std::size_t>(sent);
    }
    return true;
}

std::string RedisClient::readLine() {
    std::string line;
    char c;
    while (socket_fd_ >= 0) {
        const ssize_t received = readTransport(&c, 1);
        if (received != 1) {
            disconnect();
            return "";
        }
        if (c == '\r') {
            char next_c;
            if (readTransport(&next_c, 1) == 1 && next_c == '\n') {
                break;
            }
            disconnect();
            return "";
        } else {
            // RESP line headers and simple strings should remain small. Reject
            // malformed peers instead of growing this buffer without a bound.
            if (line.size() >= 64 * 1024) {
                disconnect();
                return "";
            }
            line += c;
        }
    }
    return line;
}

std::string RedisClient::readResponse() {
    if (socket_fd_ < 0) return "";
    std::string prefix = readLine();
    if (prefix.empty()) return "";

    char type = prefix[0];
    if (type == '-') {
        const std::string error = prefix.substr(1);
        // A promoted former primary returns READONLY while its socket can still
        // look healthy. Drop it so the next request re-queries Sentinel. The
        // current command is deliberately not replayed because its result may
        // be uncertain after failover.
        if (error.rfind("READONLY", 0) == 0 || error.rfind("MASTERDOWN", 0) == 0) {
            disconnect();
            ElasticsearchBulkLogBuffer::instance().reportAvailability(
                    sentinel_seeds_.empty() ? "redis" : "redis-sentinel", false,
                    {{"error", error.substr(0, 64)}});
        }
        return std::string(1, kRedisErrorMarker) + error;
    } else if (type == '+' || type == ':') {
        return prefix.substr(1);
    } else if (type == '$') {
        long long parsed_length = 0;
        if (!parseRespLength(prefix.substr(1), parsed_length)
                || parsed_length < -1
                || parsed_length > static_cast<long long>(kMaximumRedisBulkReplyBytes)) {
            disconnect();
            return "";
        }
        if (parsed_length == -1) return "";
        const auto length = static_cast<std::size_t>(parsed_length);
        std::string result(length, '\0');
        ssize_t total = 0;
        while (static_cast<std::size_t>(total) < length) {
            ssize_t r = readTransport(result.data() + total, length - static_cast<std::size_t>(total));
            if (r <= 0) break;
            total += r;
        }
        // consume \r\n
        char crlf[2];
        ssize_t crlf_total = 0;
        while (crlf_total < 2) {
            ssize_t r = readTransport(crlf + crlf_total, 2 - crlf_total);
            if (r <= 0) break;
            crlf_total += r;
        }
        if (static_cast<std::size_t>(total) != length || crlf_total != 2
                || crlf[0] != '\r' || crlf[1] != '\n') {
            disconnect();
            return "";
        }
        return result;
    } else if (type == '*') {
        long long parsed_count = 0;
        if (!parseRespLength(prefix.substr(1), parsed_count)
                || parsed_count < -1
                || parsed_count > static_cast<long long>(kMaximumRedisArrayElements)) {
            disconnect();
            return "";
        }
        if (parsed_count == -1) return "";
        std::string result;
        for (long long i = 0; i < parsed_count; ++i) {
            std::string item = readResponse();
            if (i > 0) result += " ";
            result += item;
        }
        return result;
    }
    return prefix;
}

bool RedisClient::ping() {
    if (!sendCommand({"PING"})) return false;
    std::string res = readResponse();
    return res == "PONG";
}

std::optional<std::string> RedisClient::execute(const std::vector<std::string>& command) {
    if (!sendCommand(command)) return std::nullopt;
    auto response = readResponse();
    // A disconnected transport must not be mistaken for an empty/nil reply.
    if (socket_fd_ < 0) return std::nullopt;
    return response;
}
