#include "memory/LruMemory.hpp"
#include "service_memory/RegistryServiceMemory.hpp"
#include "service_memory/CanvasSnapshotMemory.hpp"
#include "TestSupport.hpp"
#include <memory>
#include <optional>

struct SqlMemoryStub final : SqlExecutor {
    std::optional<SqlQueryResult> response;
    std::optional<SqlCommand> command;
    bool execute_result{true};
    std::optional<SqlQueryResult> query(const SqlCommand& sql) override { command = sql; return response; }
    bool execute(const SqlCommand& sql) override { command = sql; return execute_result; }
};
SqlQueryResult result(std::initializer_list<std::optional<std::string>> values) {
    SqlResultSet set; set.rows.push_back({std::vector<std::optional<std::string>>(values)});
    SqlQueryResult query; query.result_sets.push_back(std::move(set)); return query;
}
struct DocumentMemoryStub final : JsonDocumentStore {
    nlohmann::json document, patch, search;
    std::string index, id;
    bool found{true}; bool write_result{true};
    std::optional<nlohmann::json> getDocument(const std::string& target, const std::string& key) override {
        index = target; id = key; return found ? std::optional<nlohmann::json>(document) : std::nullopt;
    }
    bool putDocument(const std::string& target, const std::string& key, const nlohmann::json& value) override {
        index = target; id = key; document = value; return write_result;
    }
    bool patchDocument(const std::string&, const std::string&, const nlohmann::json& value) override {
        patch = value; return write_result;
    }
    bool deleteDocument(const std::string&, const std::string&) override { return true; }
    std::optional<nlohmann::json> searchDocuments(const std::string&, const nlohmann::json&) override { return search; }
};
void opaqueLruEvictsByAccessAndKeepsReturnedValuesAlive() {
    LruMemory<std::string> memory(2);
    memory.add("arbitrary/path", "first"); memory.add("other", "second");
    auto retained = memory.get("arbitrary/path");
    memory.add("third", "third");
    AGORA_CHECK(!memory.get("other"));
    memory.remove("arbitrary/path");
    AGORA_CHECK(!memory.get("arbitrary/path") && *retained == "first");
    LruMemory<int> disabled(0); disabled.add("x", 1); AGORA_CHECK(!disabled.get("x"));
}
void registryMapsRowsAndFailsClosedWithoutDriverTypes() {
    auto storage = std::make_unique<SqlMemoryStub>(); auto* stub = storage.get();
    RegistryServiceMemory registry(std::move(storage));
    stub->response = result({"42"});
    const std::string nickname = "x'; DROP TABLE users;--";
    AGORA_CHECK(registry.getActiveUserId(nickname, 7) == 42);
    AGORA_CHECK(stub->command->statement().find(nickname) == std::string::npos);
    AGORA_CHECK(stub->command->parameters().front().text_value == nickname);
    stub->response = std::nullopt;
    AGORA_CHECK(registry.getActiveUserId(nickname, 7) == -1);
    AGORA_CHECK(!registry.updateCanvasUncached(1, "node", 8000));
    stub->response = result({"1"});
    AGORA_CHECK(registry.updateCanvasUncached(1, "node", 8000));
    AGORA_CHECK(stub->command->statement().find("UPDLOCK, HOLDLOCK, ROWLOCK") != std::string::npos);
    AGORA_CHECK(stub->command->statement().find("user_sessions") == std::string::npos);
    stub->response = result({"1", "node", "8000", "redis", "6379"});
    auto assignment = registry.getCanvasStorageAssignment(1);
    AGORA_CHECK(assignment && assignment->is_cached && assignment->redis_port == 6379);
    stub->response = result({"1", "node", "8000", "redis", "6379garbage"});
    AGORA_CHECK(!registry.getCanvasStorageAssignment(1));
}
void allocationPreservesTransactionAndCandidateMapping() {
    auto storage = std::make_unique<SqlMemoryStub>(); auto* stub = storage.get();
    RegistryServiceMemory registry(std::move(storage));
    stub->response = result({"redis2", "6379", "0", "P2C", "2", "redis1", "6379", "8", "redis2", "6379", "2", "2"});
    auto allocation = registry.getOrAllocateRedisAndSetCached(7, "node", 8000);
    AGORA_CHECK(allocation.redis_ip == "redis2" && allocation.candidates.size() == 2);
    AGORA_CHECK(allocation.selected_cached_canvas_count == 2 && !allocation.was_cached);
    AGORA_CHECK(stub->command->statement().find("BEGIN TRAN;") != std::string::npos);
    AGORA_CHECK(stub->command->statement().find("ROLLBACK TRAN;") != std::string::npos);
    stub->response = result({"WRONG_SERVER", "0", "1", "NONE", "0", "", "", "", "", "", "", ""});
    AGORA_CHECK(registry.getOrAllocateRedisAndSetCached(7, "node", 8000).redis_ip == "WRONG_SERVER");
    stub->response = std::nullopt;
    AGORA_CHECK(registry.getOrAllocateRedisAndSetCached(7, "node", 8000).redis_ip == "ERROR");
}
void snapshotsOwnEncodingAndStorageSeesOnlyDocuments() {
    auto storage = std::make_unique<DocumentMemoryStub>(); auto* stub = storage.get();
    CanvasSnapshotMemory snapshots(std::move(storage), "canvas-index");
    nlohmann::json doc = {{"canvas-id", 7}, {"items", {{"large", {{"text", std::string(20000, 'x')}}}}}};
    AGORA_CHECK(snapshots.saveCanvasDocument(7, doc));
    AGORA_CHECK(stub->index == "canvas-index" && stub->id == "7");
    AGORA_CHECK(!stub->document.contains("items") && stub->document["items-b64"].size() > 1);
    AGORA_CHECK(snapshots.getCanvasDocument(7) == doc);
    stub->write_result = false; AGORA_CHECK(!snapshots.saveCanvasDocument(7, doc));
    stub->write_result = true; stub->found = false;
    stub->search = {{"hits", {{"hits", nlohmann::json::array({{{"_source", stub->document}}})}}}};
    AGORA_CHECK(snapshots.getCanvasDocument(7) == doc);
    stub->found = true; stub->document["items-b64"] = nlohmann::json::array({"invalid"});
    AGORA_CHECK(!snapshots.getCanvasDocument(7));
    AGORA_CHECK(!snapshots.saveCanvasDocument(7, {{"items", 42}}));
    AGORA_CHECK(snapshots.patchCanvasFields(7, {{"canvas-name", "updated"}}));
    AGORA_CHECK(stub->patch["canvas-name"] == "updated");
}
int main() {
    return runTest("opaque LRU lifetime and eviction", opaqueLruEvictsByAccessAndKeepsReturnedValuesAlive)
        + runTest("registry mapping and fail-safe reads", registryMapsRowsAndFailsClosedWithoutDriverTypes)
        + runTest("allocation transaction and candidate mapping", allocationPreservesTransactionAndCandidateMapping)
        + runTest("snapshot encoding independent of document storage", snapshotsOwnEncodingAndStorageSeesOnlyDocuments);
}
