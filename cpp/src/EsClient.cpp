#include "EsClient.hpp"
#include "CanvasPassword.hpp"
#include <httplib.h>
#include <iostream>
#include <cstdlib>

namespace {
std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    const char* configured = std::getenv(name);
    return configured ? configured : "";
}
}

EsClient::EsClient(const std::string& host, int port,
                   const std::string& user,
                   const std::string& pass,
                   const std::string& index)
    : host_(host), port_(port), user_(envOr("ES_USER_NAME", user)),
      pass_(envOr("ES_USER_PASSWORD", pass)), index_(envOr("ES_INDEX", index)) {
}

std::optional<nlohmann::json> EsClient::getCanvasDocument(int canvasId) {
    if (user_.empty() || pass_.empty() || index_.empty()) {
        std::cerr << "[EsClient] ES_USER_NAME, ES_USER_PASSWORD and ES_INDEX must be configured\n";
        return std::nullopt;
    }
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

bool EsClient::saveCanvasDocument(int canvasId, const nlohmann::json& doc) {
    if (user_.empty() || pass_.empty() || index_.empty()) {
        std::cerr << "[EsClient] Elasticsearch credentials are not configured\n";
        return false;
    }
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(3, 0);
    cli.set_basic_auth(user_, pass_);

    nlohmann::json safe_doc = doc;
    if (safe_doc.contains("canvas-password-hash") && safe_doc["canvas-password-hash"].is_string()) {
        auto normalized = normalizeCanvasPassword(safe_doc["canvas-password-hash"].get<std::string>());
        if (!normalized) return false;
        safe_doc["canvas-password-hash"] = *normalized;
    }
    for (const char* legacy : {"canvas-password", "canvasPassword", "canvas_password_hash"}) {
        if (!safe_doc.contains(legacy)) continue;
        if (!safe_doc.contains("canvas-password-hash") && safe_doc[legacy].is_string()) {
            auto normalized = normalizeCanvasPassword(safe_doc[legacy].get<std::string>());
            if (!normalized) return false;
            safe_doc["canvas-password-hash"] = *normalized;
        }
        safe_doc.erase(legacy);
    }
    std::string doc_path = "/" + index_ + "/_doc/" + std::to_string(canvasId);
    auto res = cli.Put(doc_path, safe_doc.dump(), "application/json");
    if (res && (res->status == 200 || res->status == 201)) {
        std::cout << "[EsClient] Successfully reflected canvas #" << canvasId << " from Redis to Elasticsearch\n";
        return true;
    }
    std::cerr << "[EsClient] Failed to save canvas #" << canvasId << " to Elasticsearch: "
              << (res ? std::to_string(res->status) : "connection error") << "\n";
    return false;
}
