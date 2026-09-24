#include "EsClient.hpp"
#include "CanvasPassword.hpp"
#include <httplib.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <openssl/evp.h>

namespace {
std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    const char* configured = std::getenv(name);
    return configured ? configured : "";
}

// Each encoded chunk stays below Elasticsearch's maximum indexed term size
// even when an item contains a large uninterrupted value such as base64 data.
constexpr std::size_t ITEMS_CHUNK_BYTES = 8190;

nlohmann::json encodeItems(const nlohmann::json& items) {
    const std::string raw = items.dump();
    nlohmann::json chunks = nlohmann::json::array();
    for (std::size_t offset = 0; offset < raw.size(); offset += ITEMS_CHUNK_BYTES) {
        const auto length = std::min(ITEMS_CHUNK_BYTES, raw.size() - offset);
        std::string encoded(4 * ((length + 2) / 3), '\0');
        EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
                        reinterpret_cast<const unsigned char*>(raw.data() + offset),
                        static_cast<int>(length));
        chunks.push_back(std::move(encoded));
    }
    return chunks;
}

std::optional<nlohmann::json> restoreItems(nlohmann::json source) {
    if (!source.is_object() || !source.contains("items-b64")) return source;
    const auto& chunks = source["items-b64"];
    if (!chunks.is_array() || chunks.empty()) return std::nullopt;
    std::string raw;
    for (const auto& chunk : chunks) {
        if (!chunk.is_string()) return std::nullopt;
        const auto encoded = chunk.get<std::string>();
        if (encoded.empty() || encoded.size() % 4 != 0 || encoded.size() > 4 * ((ITEMS_CHUNK_BYTES + 2) / 3)) {
            return std::nullopt;
        }
        std::string decoded(encoded.size() / 4 * 3, '\0');
        const int bytes = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(decoded.data()),
                                          reinterpret_cast<const unsigned char*>(encoded.data()),
                                          static_cast<int>(encoded.size()));
        if (bytes < 0) return std::nullopt;
        std::size_t padding = 0;
        if (encoded.back() == '=') ++padding;
        if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') ++padding;
        if (static_cast<std::size_t>(bytes) < padding) return std::nullopt;
        decoded.resize(static_cast<std::size_t>(bytes) - padding);
        raw += decoded;
    }
    try {
        auto items = nlohmann::json::parse(raw);
        if (!items.is_object()) return std::nullopt;
        source["items"] = std::move(items);
        source.erase("items-b64");
        return source;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
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
    cli.set_write_timeout(3, 0);
    cli.set_basic_auth(user_, pass_);

    // 1. Direct doc lookup by ID
    std::string direct_path = "/" + index_ + "/_doc/" + std::to_string(canvasId);
    auto res = cli.Get(direct_path);
    if (res && res->status == 200) {
        try {
            auto json_res = nlohmann::json::parse(res->body);
            if (json_res.contains("_source")) {
                auto restored = restoreItems(json_res["_source"]);
                if (!restored) std::cerr << "[EsClient] Invalid encoded items for canvas #" << canvasId << "\n";
                return restored;
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
                    auto restored = restoreItems(hits[0]["_source"]);
                    if (!restored) std::cerr << "[EsClient] Invalid encoded items for canvas #" << canvasId << "\n";
                    return restored;
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
    cli.set_write_timeout(3, 0);
    cli.set_basic_auth(user_, pass_);

    nlohmann::json safe_doc = doc;
    if (safe_doc.contains("items")) {
        if (!safe_doc["items"].is_object()) {
            std::cerr << "[EsClient] Refusing to save canvas #" << canvasId << " with invalid items\n";
            return false;
        }
        safe_doc["items-b64"] = encodeItems(safe_doc["items"]);
        safe_doc.erase("items");
    }
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
    const std::string payload = safe_doc.dump();
    std::cout << "[EsClient] Saving canvas #" << canvasId
              << " to Elasticsearch (bytes=" << payload.size() << ")\n";
    const auto started = std::chrono::steady_clock::now();
    auto res = cli.Put(doc_path, payload, "application/json");
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (res && (res->status == 200 || res->status == 201)) {
        std::cout << "[EsClient] Successfully reflected canvas #" << canvasId
                  << " from Redis to Elasticsearch (elapsed_ms=" << elapsed_ms << ")\n";
        return true;
    }
    std::cerr << "[EsClient] Failed to save canvas #" << canvasId << " to Elasticsearch: "
              << (res ? "HTTP " + std::to_string(res->status)
                      : "transport " + httplib::to_string(res.error()))
              << " (elapsed_ms=" << elapsed_ms << ")\n";
    if (res && !res->body.empty()) {
        std::cerr << "[EsClient] Elasticsearch error response: "
                  << res->body.substr(0, 2048) << "\n";
    }
    return false;
}

bool EsClient::patchCanvasFields(int canvasId, const std::map<std::string, nlohmann::json>& fields) {
    if (fields.empty() || user_.empty() || pass_.empty() || index_.empty()) {
        std::cerr << "[EsClient] Cannot patch canvas settings: fields or Elasticsearch credentials are missing\n";
        return false;
    }
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(3, 0);
    cli.set_basic_auth(user_, pass_);

    nlohmann::json normalized = nlohmann::json::object();
    for (const auto& [field, value] : fields) {
        if (field == "canvas-password-hash" && value.is_string()) {
            auto password = normalizeCanvasPassword(value.get<std::string>());
            if (!password) return false;
            normalized[field] = *password;
        } else {
            normalized[field] = value;
        }
    }
    const std::string path = "/" + index_ + "/_update/" + std::to_string(canvasId)
        + "?retry_on_conflict=3&refresh=true";
    const nlohmann::json body = {{"doc", normalized}};
    auto res = cli.Post(path, body.dump(), "application/json");
    if (res && res->status >= 200 && res->status < 300) return true;
    std::cerr << "[EsClient] Failed to patch canvas #" << canvasId << " in Elasticsearch: "
              << (res ? std::to_string(res->status) : "connection error") << "\n";
    return false;
}
