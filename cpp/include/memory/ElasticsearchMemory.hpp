#pragma once
#include "memory/JsonDocumentStore.hpp"
class ElasticsearchMemory final : public JsonDocumentStore {
public:
    ElasticsearchMemory(const std::string& host = "127.0.0.1", int port = 9200,
                        const std::string& user = {}, const std::string& password = {});
    std::optional<nlohmann::json> getDocument(const std::string& index, const std::string& document_id) override;
    bool putDocument(const std::string& index, const std::string& document_id,
                     const nlohmann::json& document) override;
    bool patchDocument(const std::string& index, const std::string& document_id,
                       const nlohmann::json& fields) override;
    bool deleteDocument(const std::string& index, const std::string& document_id) override;
    std::optional<nlohmann::json> searchDocuments(const std::string& index,
                                                   const nlohmann::json& query) override;

private:
    std::string host_; int port_; std::string user_, pass_;
};
