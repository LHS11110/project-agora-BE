#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <optional>
#include <sys/types.h>
#include <utility>
#include <openssl/ssl.h>
#include "RedisCommandExecutor.hpp"

// Sentinel discovery, verified TLS and RESP transport only; no cache policy.
// Keep each instance confined to a single operation/request thread.
class RedisClient final : public RedisCommandExecutor {
public:
    RedisClient(const std::string& host = "127.0.0.1", int port = 6379,
                const std::string& user = "", const std::string& password = "");
    ~RedisClient() override;
    bool connect() override;
    void disconnect() override;
    bool ping();
    std::optional<std::string> execute(const std::vector<std::string>& command) override;

private:
    std::string user_;
    std::string password_;
    std::string sentinel_master_name_;
    std::string sentinel_user_;
    std::string sentinel_password_;
    std::vector<std::pair<std::string, int>> sentinel_seeds_;
    std::string tls_ca_cert_;
    bool tls_enabled_;
    bool tls_config_valid_;
    int socket_fd_;
    SSL_CTX* ssl_context_;
    SSL* ssl_;

    bool connectTo(const std::string& host, int port, int timeout_ms);
    ssize_t readTransport(void* buffer, std::size_t size);
    ssize_t writeTransport(const void* buffer, std::size_t size);
    bool sendCommand(const std::vector<std::string>& args);
    std::string readResponse();
    std::string readLine();
};
