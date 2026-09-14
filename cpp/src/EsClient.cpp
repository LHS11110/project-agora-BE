#include "EsClient.hpp"
#include <httplib.h>
#include <iostream>

EsClient::EsClient(const std::string& host, int port,
                   const std::string& user,
                   const std::string& pass,
                   const std::string& index)
    : host_(host), port_(port), user_(user), pass_(pass), index_(index) {
}

std::optional<nlohmann::json> EsClient::getCanvasDocument(int canvasId) {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(3, 0);
    cli.set_basic_auth(user_, pass_);

    // 1. Direct doc lookup by ID
    std::string direct_path = "/" + index_ + "/_doc/" + std::to_string(canvasId);
    auto res = cli.Get(direct_path);
    if (res && res->status == 200) {
        try {
            auto json_res = nlohmann::json::parse(res->body);
            if (json_res.contains("_source")) {
                return json_res["_source"];
            }
        } catch (...) {
        }
    }

    // 2. Term search by canvas-id
    std::string search_path = "/" + index_ + "/_search";
    nlohmann::json query = {
        {"query", {{"term", {{"canvas-id", canvasId}}}}},
        {"size", 1}
    };

    res = cli.Post(search_path, query.dump(), "application/json");
    if (res && res->status == 200) {
        try {
            auto json_res = nlohmann::json::parse(res->body);
            if (json_res.contains("hits") && json_res["hits"].contains("hits")) {
                auto hits = json_res["hits"]["hits"];
                if (hits.is_array() && !hits.empty()) {
                    return hits[0]["_source"];
                }
            }
        } catch (...) {
        }
    }

    std::cerr << "[EsClient] Document for canvas #" << canvasId << " not found in Elasticsearch\n";
    return std::nullopt;
}
