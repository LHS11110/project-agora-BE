#pragma once

#include <string>
#include <optional>
#include <map>
#include <nlohmann/json.hpp>
#include "JsonDocumentStore.hpp"

class EsClient : public JsonDocumentStore {
public:
    EsClient(const std::string& host = "127.0.0.1", int port = 9200,
             const std::string& user = "",
             const std::string& pass = "",
             const std::string& index = "");

    // Generic document operations for feature-specific Elasticsearch repositories.
    std::optional<nlohmann::json> getDocument(const std::string& index, const std::string& document_id) override;
    bool putDocument(const std::string& index, const std::string& document_id,
                     const nlohmann::json& document) override;
    bool patchDocument(const std::string& index, const std::string& document_id,
                       const nlohmann::json& fields) override;
    bool deleteDocument(const std::string& index, const std::string& document_id) override;
    std::optional<nlohmann::json> searchDocuments(const std::string& index,
                                                   const nlohmann::json& query) override;

    std::optional<nlohmann::json> getCanvasDocument(int canvasId);
    bool saveCanvasDocument(int canvasId, const nlohmann::json& doc);
    bool patchCanvasFields(int canvasId, const std::map<std::string, nlohmann::json>& fields);

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string pass_;
    std::string index_;
};
