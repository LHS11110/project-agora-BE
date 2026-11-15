#include "service_memory/CanvasServiceMemory.hpp"
#include "memory/RedisCommandExecutor.hpp"
#include "service_memory/CanvasSnapshotMemory.hpp"
#include "TestSupport.hpp"
#include <cstdlib>
#include <deque>
#include <memory>

struct Exchange {
    std::vector<std::string> command;
    std::optional<std::string> reply;
};
struct ScriptedTransport final : RedisCommandExecutor {
    std::deque<Exchange> exchanges;
    bool connect() override { return true; }
    void disconnect() override {}
    std::optional<std::string> execute(const std::vector<std::string>& command) override {
        AGORA_CHECK(!exchanges.empty());
        auto expected = exchanges.front();
        exchanges.pop_front();
        AGORA_CHECK(command == expected.command);
        return expected.reply;
    }
};

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
void dirtyCacheSurvivesTransportFailureAndReplacement() {
    const std::string key = "canvas:9001";
    const std::string item = "$[\"items\"][\"hot\"]";
    const nlohmann::json document = {{"items", {{"hot", {{"value", 1}}}}}};
    auto transport = std::make_unique<ScriptedTransport>();
    auto* script = transport.get();
    CanvasServiceMemory memory(std::move(transport));
    script->exchanges.push_back({{"JSON.SET", key, "$", document.dump()}, "OK"});
    AGORA_CHECK(memory.storeCanvas(9001, document));
    script->exchanges.push_back({{"JSON.GET", key, item + "[\"value\"]"}, "[1]"});
    AGORA_CHECK(memory.readItem(9001, "hot", "value") == "[1]");
    script->exchanges.push_back({{"JSON.GET", key, item}, "[{\"value\":1}]"});
    AGORA_CHECK(memory.readItem(9001, "hot", "value") == "[1]");
    AGORA_CHECK(memory.storeItem(9001, "hot", {{"value", 2}}));
    AGORA_CHECK(memory.readItem(9001, "hot", "value") == "[2]");
    script->exchanges.push_back({{"JSON.GET", key}, document.dump()});
    auto loaded = memory.loadCanvas(9001);
    AGORA_CHECK(loaded.has_value());
    AGORA_CHECK(nlohmann::json::parse(*loaded)["items"]["hot"]["value"] == 2);
    script->exchanges.push_back({{"JSON.SET", key, item, "{\"value\":2}"}, std::nullopt});
    AGORA_CHECK(!memory.flushCanvas(9001));
    AGORA_CHECK(memory.readItem(9001, "hot", "value") == "[2]");
    script->exchanges.push_back({{"JSON.SET", key, item, "{\"value\":2}"}, "OK"});
    AGORA_CHECK(memory.flushCanvas(9001));
    AGORA_CHECK(script->exchanges.empty());

    auto replacement = std::make_unique<ScriptedTransport>();
    auto* next = replacement.get();
    CanvasServiceMemory another(std::move(replacement));
    AGORA_CHECK(another.readItem(9001, "hot", "value") == "[2]");
    next->exchanges.push_back({{"JSON.DEL", key, item}, "1"});
    AGORA_CHECK(another.removeItem(9001, "hot"));
    next->exchanges.push_back({{"JSON.GET", key, item + "[\"value\"]"}, "[]"});
    AGORA_CHECK(another.readItem(9001, "hot", "value") == "[]");
    AGORA_CHECK(next->exchanges.empty());
}

void domainIdsAreEscapedBeforeStorage() {
    auto transport = std::make_unique<ScriptedTransport>();
    auto* script = transport.get();
    CanvasServiceMemory memory(std::move(transport));
    const std::string id = "quoted\"\\id";
    const std::string path = "$[\"items\"][" + nlohmann::json(id).dump() + "]";
    script->exchanges.push_back({{"JSON.SET", "canvas:9002", path, "{\"value\":3}"}, "OK"});
    AGORA_CHECK(memory.storeItem(9002, id, {{"value", 3}}));
    script->exchanges.push_back({{"JSON.GET", "canvas:9002", path + "[\"value\"]"}, "[3]"});
    AGORA_CHECK(memory.readItem(9002, id, "value") == "[3]");
    AGORA_CHECK(script->exchanges.empty());
}

void durableDocumentRestorationBelongsToCanvasMemory() {
    auto backend = std::make_unique<DocumentMemoryStub>(); auto* durable = backend.get();
    durable->document = {{"canvas-id", 9010}, {"items", nlohmann::json::object()}};
    CanvasSnapshotMemory snapshots(std::move(backend), "canvas-index");
    auto transport = std::make_unique<ScriptedTransport>(); auto* script = transport.get();
    CanvasServiceMemory memory(std::move(transport));
    auto expected = durable->document; expected["_cache_generation"] = "generation-1";
    script->exchanges.push_back({{"JSON.SET", "canvas:9010", "$", expected.dump()}, "OK"});
    AGORA_CHECK(memory.initializeCanvas(9010, snapshots, false, "generation-1") == expected);
    AGORA_CHECK(durable->index == "canvas-index" && durable->id == "9010");
    script->exchanges.push_back({{"JSON.GET", "canvas:9010"}, std::nullopt});
    durable->id = "untouched";
    AGORA_CHECK(!memory.initializeCanvas(9010, snapshots, true, "generation-2"));
    AGORA_CHECK(durable->id == "untouched"); // Active data never falls back to stale snapshots.
    AGORA_CHECK(script->exchanges.empty());
}
struct BrokerTransport final : RedisCommandExecutor {
    std::vector<std::string> last;
    bool connect() override{return true;}void disconnect() override{}
    std::optional<std::string> execute(const std::vector<std::string>& command) override{
        last=command;
        if(command[0]=="JSON.GET"){
            if(command.size()==3)return "[{\"kind\":\"shape\"}]";
            return nlohmann::json({{command[2],nlohmann::json::array({{{"kind","shape"}}})},{command[3],nlohmann::json::array()}}).dump();
        }
        return "{\"revision\":1,\"server_revision\":1,\"accepted_count\":1,\"duplicate\":false}";
    }
};
void brokerStoragePreservesJsonAndReadsObjectsTogether(){
    auto transport=std::make_unique<BrokerTransport>();auto* wire=transport.get();CanvasServiceMemory memory(std::move(transport));
    const nlohmann::json item={{"kind","text"},{"automerge_changes",nlohmann::json::array()},{"x",0.1234567890123456}};
    auto result=memory.applyBrokerBatch(9997,"7:tab",1,"request","digest",{{"text",item}},{{"text",-1}});
    AGORA_CHECK(result&&(*result)["revision"]==1);
    AGORA_CHECK(wire->last[0]=="EVAL");
    const auto encoded=nlohmann::json::parse(wire->last[8]);
    AGORA_CHECK(encoded["text"].is_string()&&nlohmann::json::parse(encoded["text"].get<std::string>())==item);
    const auto selected=memory.readBrokerItems(9997,{"escaped\"id","missing"});
    AGORA_CHECK(selected&&(*selected)["escaped\"id"]["kind"]=="shape"&&(*selected)["missing"].is_null());
    AGORA_CHECK(wire->last.size()==4&&wire->last[2]=="$[\"items\"][\"escaped\\\"id\"]");
    const auto single=memory.readBrokerItems(9997,{"one"});AGORA_CHECK(single&&(*single)["one"]["kind"]=="shape");
}
void workspaceAndCanvasIsolation() {
    auto transport = std::make_unique<ScriptedTransport>();
    auto* script = transport.get();
    CanvasServiceMemory memory(std::move(transport));

    const nlohmann::json doc{{"items", {{"cell-1", {{"kind", "code"}, {"code", "x=1"}}}}}};
    script->exchanges.push_back({{"JSON.SET", "canvas:42", "$", doc.dump()}, "OK"});
    AGORA_CHECK(memory.storeCanvas(SpaceScope::Canvas, 42, doc));
    script->exchanges.push_back({{"JSON.SET", "workspace:42", "$", doc.dump()}, "OK"});
    AGORA_CHECK(memory.storeCanvas(SpaceScope::Workspace, 42, doc));

    const nlohmann::json ws_item{{"kind", "code"}, {"code", "ws"}};
    script->exchanges.push_back({{"JSON.SET", "workspace:42", "$[\"items\"][\"cell-1\"]", ws_item.dump()}, "OK"});
    AGORA_CHECK(memory.storeItem(SpaceScope::Workspace, 42, "cell-1", ws_item));
    script->exchanges.push_back({{"JSON.GET", "canvas:42", "$[\"items\"][\"cell-1\"][\"code\"]"}, "[\"cv\"]"});
    AGORA_CHECK(memory.readItem(SpaceScope::Canvas, 42, "cell-1", "code") == "[\"cv\"]");
    script->exchanges.push_back({{"JSON.GET", "workspace:42", "$[\"items\"][\"cell-1\"][\"code\"]"}, "[\"ws\"]"});
    AGORA_CHECK(memory.readItem(SpaceScope::Workspace, 42, "cell-1", "code") == "[\"ws\"]");

    script->exchanges.push_back({{"JSON.DEL", "workspace:42", "$[\"items\"][\"cell-1\"]"}, "1"});
    AGORA_CHECK(memory.removeItem(SpaceScope::Workspace, 42, "cell-1"));
    AGORA_CHECK(script->exchanges.empty());

    auto backend = std::make_unique<DocumentMemoryStub>();
    auto* durable = backend.get();
    CanvasSnapshotMemory ws_snapshots(std::move(backend), "workspace", SpaceScope::Workspace);
    AGORA_CHECK(ws_snapshots.scope() == SpaceScope::Workspace && ws_snapshots.indexName() == "workspace");
    AGORA_CHECK(ws_snapshots.saveCanvasDocument(7, {{"canvas-id", 7}, {"canvas-name", "Proofs"}, {"items", nlohmann::json::object()}}));
    AGORA_CHECK(durable->index == "workspace" && durable->id == "7");
    AGORA_CHECK(!durable->document.contains("canvas-id") && !durable->document.contains("canvas-name"));
    AGORA_CHECK(durable->document["workspace-id"] == 7 && durable->document["workspace-name"] == "Proofs");
    auto restored = ws_snapshots.getCanvasDocument(7);
    AGORA_CHECK(restored && (*restored)["canvas-id"] == 7 && (*restored)["canvas-name"] == "Proofs");
}

int main() {
    setenv("CPP_CANVAS_LRU_CAPACITY", "2", 1);
    setenv("CPP_CANVAS_LRU_ITEMS_PER_CANVAS", "1", 1);
    return runTest("broker JSON encoding and grouped reads",brokerStoragePreservesJsonAndReadsObjectsTogether)
        + runTest("dirty cache survives failed flush and transport replacement", dirtyCacheSurvivesTransportFailureAndReplacement)
        + runTest("domain IDs are escaped at the memory boundary", domainIdsAreEscapedBeforeStorage)
        + runTest("canvas memory restores durable documents and protects active data", durableDocumentRestorationBelongsToCanvasMemory)
        + runTest("workspace and canvas storage keys and snapshots are isolated", workspaceAndCanvasIsolation);
}
