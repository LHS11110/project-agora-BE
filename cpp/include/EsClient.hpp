#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>

class EsClient {
public:
    EsClient(const std::string& host = "127.0.0.1", int port = 9200,
             const std::string& user = "agora_user",
             const std::string& pass = "AgoraUserSecret@Passw0rd!2026",
             const std::string& index = "canvas");

    std::optional<nlohmann::json> getCanvasDocument(int canvasId);

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string pass_;
    std::string index_;
};
