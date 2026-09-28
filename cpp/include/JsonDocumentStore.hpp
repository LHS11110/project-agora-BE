#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

/** Document-store port used by feature repositories. */
class JsonDocumentStore {
public:
    virtual ~JsonDocumentStore() = default;
    virtual std::optional<nlohmann::json> getDocument(
            const std::string& index, const std::string& document_id) = 0;
    virtual bool putDocument(const std::string& index, const std::string& document_id,
                             const nlohmann::json& document) = 0;
    virtual bool patchDocument(const std::string& index, const std::string& document_id,
                               const nlohmann::json& fields) = 0;
    virtual bool deleteDocument(const std::string& index, const std::string& document_id) = 0;
    virtual std::optional<nlohmann::json> searchDocuments(
            const std::string& index, const nlohmann::json& query) = 0;
};
