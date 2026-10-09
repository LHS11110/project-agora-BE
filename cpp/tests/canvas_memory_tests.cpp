#include "CanvasMemory.hpp"
#include "RedisCommandExecutor.hpp"
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

void dirtyCacheSurvivesTransportFailureAndReplacement() {
    const std::string key = "canvas:9001";
    const std::string item = "$[\"items\"][\"hot\"]";
    const nlohmann::json document = {{"items", {{"hot", {{"value", 1}}}}}};
    auto transport = std::make_unique<ScriptedTransport>();
    auto* script = transport.get();
    CanvasMemory memory(std::move(transport));
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
    CanvasMemory another(std::move(replacement));
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
    CanvasMemory memory(std::move(transport));
    const std::string id = "quoted\"\\id";
    const std::string path = "$[\"items\"][" + nlohmann::json(id).dump() + "]";
    script->exchanges.push_back({{"JSON.SET", "canvas:9002", path, "{\"value\":3}"}, "OK"});
    AGORA_CHECK(memory.storeItem(9002, id, {{"value", 3}}));
    script->exchanges.push_back({{"JSON.GET", "canvas:9002", path + "[\"value\"]"}, "[3]"});
    AGORA_CHECK(memory.readItem(9002, id, "value") == "[3]");
    AGORA_CHECK(script->exchanges.empty());
}

int main() {
    setenv("CPP_CANVAS_LRU_CAPACITY", "2", 1);
    setenv("CPP_CANVAS_LRU_ITEMS_PER_CANVAS", "1", 1);
    return runTest("dirty cache survives failed flush and transport replacement", dirtyCacheSurvivesTransportFailureAndReplacement)
        + runTest("domain IDs are escaped at the memory boundary", domainIdsAreEscapedBeforeStorage);
}
