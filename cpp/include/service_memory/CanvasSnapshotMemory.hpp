#pragma once

#include <string>
#include <optional>
#include <map>
#include <nlohmann/json.hpp>
#include "SpaceScope.hpp"
#include "memory/JsonDocumentStore.hpp"
#include <memory>

class CanvasSnapshotMemory final {
public:
    CanvasSnapshotMemory(const std::string& host = "127.0.0.1", int port = 9200,
             const std::string& user = "",
             const std::string& pass = "",
             const std::string& index = "",
             SpaceScope scope = SpaceScope::Canvas);

    CanvasSnapshotMemory(std::unique_ptr<JsonDocumentStore> storage, std::string index,
                         SpaceScope scope = SpaceScope::Canvas);

    SpaceScope scope() const noexcept { return scope_; }
    const std::string& indexName() const noexcept { return index_; }
    std::optional<nlohmann::json> getCanvasDocument(int canvasId);
    bool saveCanvasDocument(int canvasId, const nlohmann::json& doc);
    bool patchCanvasFields(int canvasId, const std::map<std::string, nlohmann::json>& fields);

private:
    std::unique_ptr<JsonDocumentStore> storage_;
    std::string index_;
    SpaceScope scope_{SpaceScope::Canvas};
};
