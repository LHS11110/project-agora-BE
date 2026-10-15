#include "service_memory/CanvasSnapshotMemory.hpp"
#include "memory/ElasticsearchMemory.hpp"
#include "CanvasPassword.hpp"
#include "Environment.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>
namespace {
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

std::optional<nlohmann::json> restoreItems(nlohmann::json source, const std::string& field="items", const std::string& encoded_field="items-b64") {
    if (!source.is_object() || !source.contains(encoded_field)) return source;
    const auto& chunks = source[encoded_field];
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
        source[field] = std::move(items);
        source.erase(encoded_field);
        return source;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}
std::optional<nlohmann::json> restoreDocument(nlohmann::json source) {
    auto document=restoreItems(std::move(source));
    return document?restoreItems(std::move(*document),"__broker_sync","broker-sync-b64"):std::nullopt;
}

}
CanvasSnapshotMemory::CanvasSnapshotMemory(const std::string& host, int port,
        const std::string& user, const std::string& password, const std::string& index)
    : CanvasSnapshotMemory(std::make_unique<ElasticsearchMemory>(host, port, user, password),
                           index.empty() ? environmentValue("ES_INDEX") : index) {}
CanvasSnapshotMemory::CanvasSnapshotMemory(std::unique_ptr<JsonDocumentStore> storage, std::string index)
    : storage_(std::move(storage)), index_(std::move(index)) {
    if (!storage_) throw std::invalid_argument("CanvasSnapshotMemory storage must not be null");
}
std::optional<nlohmann::json> CanvasSnapshotMemory::getCanvasDocument(int canvasId) {
    if (index_.empty()) return std::nullopt;
    if (const auto document = storage_->getDocument(index_, std::to_string(canvasId)))
        return restoreDocument(*document);
    const nlohmann::json query = {{"query", {{"term", {{"canvas-id", canvasId}}}}}, {"size", 1}};
    const auto response = storage_->searchDocuments(index_, query);
    if (!response) return std::nullopt;
    try {
        const auto& hits = response->at("hits").at("hits");
        if (hits.is_array() && !hits.empty()) return restoreDocument(hits.front().at("_source"));
    } catch (const nlohmann::json::exception&) {}
    return std::nullopt;
}
bool CanvasSnapshotMemory::saveCanvasDocument(int canvasId, const nlohmann::json& doc) {
    if (index_.empty()) return false;
    nlohmann::json safe_doc = doc;
    if (safe_doc.contains("items")) {
        if (!safe_doc["items"].is_object()) {
            std::cerr << "[EsClient] Refusing to save canvas #" << canvasId << " with invalid items\n";
            return false;
        }
        safe_doc["items-b64"] = encodeItems(safe_doc["items"]);
        safe_doc.erase("items");
    }
    // Client IDs are arbitrary keys: encode the receipt ledger to avoid an
    // Elasticsearch mapping field per browser session and preserve retry state.
    if (safe_doc.contains("__broker_sync")) {
        if (!safe_doc["__broker_sync"].is_object()) return false;
        safe_doc["broker-sync-b64"] = encodeItems(safe_doc["__broker_sync"]);
        safe_doc.erase("__broker_sync");
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
    return storage_->putDocument(index_, std::to_string(canvasId), safe_doc);
}
bool CanvasSnapshotMemory::patchCanvasFields(int canvasId, const std::map<std::string, nlohmann::json>& fields) {
    if (fields.empty() || index_.empty()) return false;
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
    return storage_->patchDocument(index_, std::to_string(canvasId), normalized);
}
