#include "memory/MemoryClass.hpp"
#include "TestSupport.hpp"
#include <deque>
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


void arbitraryDocumentsHaveTransparentPromotionAndWriteBack() {
    auto transport = std::make_unique<ScriptedTransport>(); auto* script = transport.get();
    MemoryClass memory(std::move(transport));
    const std::string key = "preferences:user/alpha";
    script->exchanges.push_back({{"JSON.GET", key, "$"}, "[{\"theme\":\"light\"}]"});
    AGORA_CHECK(memory.read(key) == "{\"theme\":\"light\"}");
    script->exchanges.push_back({{"JSON.GET", key, "$"}, "[{\"theme\":\"light\"}]"});
    AGORA_CHECK(memory.read(key) == "{\"theme\":\"light\"}");
    AGORA_CHECK(memory.write(key, {{"theme", "dark"}}));
    AGORA_CHECK(memory.read(key, "$[\"theme\"]") == "[\"dark\"]");
    script->exchanges.push_back({{"JSON.SET", key, "$", "{\"theme\":\"dark\"}"}, std::nullopt});
    AGORA_CHECK(!memory.flush(key));
    AGORA_CHECK(memory.read(key) == "{\"theme\":\"dark\"}");
    script->exchanges.push_back({{"JSON.SET", key, "$", "{\"theme\":\"dark\"}"}, "OK"});
    AGORA_CHECK(memory.flush(key));
    script->exchanges.push_back({{"JSON.GET", key, "$.*"}, "[\"dark\"]"});
    AGORA_CHECK(memory.read(key, "$.*") == "[\"dark\"]");
    AGORA_CHECK(script->exchanges.empty());
    auto second = std::make_unique<ScriptedTransport>(); auto* independent = second.get();
    MemoryClass other(std::move(second));
    independent->exchanges.push_back({{"JSON.GET", key, "$"}, "[{\"theme\":\"other\"}]"});
    AGORA_CHECK(other.read(key) == "{\"theme\":\"other\"}");
    AGORA_CHECK(independent->exchanges.empty());
}
void dirtyEvictionFailsWithoutLosingWrites() {
    MemoryClass::Options options; options.entries_per_document = 1; options.region_depth = 1;
    auto transport = std::make_unique<ScriptedTransport>(); auto* script = transport.get();
    MemoryClass memory(std::move(transport), MemoryClass::createState(options));
    script->exchanges.push_back({{"JSON.GET", "inventory", "$[\"a\"]"}, "[1]"});
    AGORA_CHECK(memory.read("inventory", "$[\"a\"]") == "[1]");
    script->exchanges.push_back({{"JSON.GET", "inventory", "$[\"a\"]"}, "[1]"});
    AGORA_CHECK(memory.read("inventory", "$[\"a\"]") == "[1]");
    AGORA_CHECK(memory.write("inventory", 2, "$[\"a\"]"));
    script->exchanges.push_back({{"JSON.GET", "inventory", "$[\"b\"]"}, "[3]"});
    AGORA_CHECK(memory.read("inventory", "$[\"b\"]") == "[3]");
    script->exchanges.push_back({{"JSON.GET", "inventory", "$[\"b\"]"}, "[3]"});
    script->exchanges.push_back({{"JSON.SET", "inventory", "$[\"a\"]", "2"}, std::nullopt});
    AGORA_CHECK(memory.read("inventory", "$[\"b\"]") == "[3]");
    AGORA_CHECK(memory.read("inventory", "$[\"a\"]") == "[2]");
    script->exchanges.push_back({{"JSON.SET", "inventory", "$[\"a\"]", "2"}, "OK"});
    script->exchanges.push_back({{"EVAL", "opaque", "1", "inventory"}, "OK"});
    AGORA_CHECK(memory.executeAtomic("inventory", {"EVAL", "opaque", "1", "inventory"}, {"$[\"a\"]"}) == "OK");
    script->exchanges.push_back({{"JSON.GET", "inventory", "$[\"a\"]"}, "[9]"});
    AGORA_CHECK(memory.read("inventory", "$[\"a\"]") == "[9]");
    AGORA_CHECK(script->exchanges.empty());
}
void disabledAndOversizedCachesWriteThrough() {
    for (bool disabled : {false, true}) {
        MemoryClass::Options options; options.maximum_entry_bytes = 1;
        if (disabled) options.entries_per_document = 0;
        auto transport = std::make_unique<ScriptedTransport>(); auto* script = transport.get();
        MemoryClass memory(std::move(transport), MemoryClass::createState(options));
        for (int i = 0; i < 3; ++i) {
            script->exchanges.push_back({{"JSON.GET", "opaque", "$"}, "[{\"x\":1}]"});
            AGORA_CHECK(memory.read("opaque") == "{\"x\":1}");
        }
        script->exchanges.push_back({{"JSON.SET", "opaque", "$", "{\"x\":2}"}, "OK"});
        AGORA_CHECK(memory.write("opaque", {{"x",2}}));
        AGORA_CHECK(script->exchanges.empty());
    }
}
void remoteCollectionsStayCoherentWithDirtyMetadata() {
    MemoryClass::Options options;
    options.remote_fields = [](const nlohmann::json&) { return std::vector<std::string>{"events"}; };
    auto transport = std::make_unique<ScriptedTransport>(); auto* script = transport.get();
    MemoryClass memory(std::move(transport), MemoryClass::createState(options));
    const std::string key = "audit:7";
    for (int i = 0; i < 2; ++i) {
        script->exchanges.push_back({{"JSON.GET", key, "$[\"label\"]"}, "[\"old\"]"});
        if (i == 1) script->exchanges.back() = {{"JSON.GET", key, "$"}, "[{\"label\":\"old\",\"events\":[1]}]"};
        AGORA_CHECK(memory.read(key, "$[\"label\"]") == "[\"old\"]");
    }
    AGORA_CHECK(memory.write(key, "new", "$[\"label\"]"));
    script->exchanges.push_back({{"JSON.GET", key, "$"}, "[{\"label\":\"old\",\"events\":[1,2]}]"});
    AGORA_CHECK(memory.read(key) == "{\"events\":[1,2],\"label\":\"new\"}");
    script->exchanges.push_back({{"JSON.GET", key, "$"}, "[{\"label\":\"old\",\"events\":[1,2]}]"});
    script->exchanges.push_back({{"JSON.SET", key, "$", "{\"events\":[1,2],\"label\":\"new\"}"}, "OK"});
    AGORA_CHECK(memory.flush(key)); AGORA_CHECK(script->exchanges.empty());
}
int main() {
    return runTest("generic promotion, write-back failures and isolated service state", arbitraryDocumentsHaveTransparentPromotionAndWriteBack)
        + runTest("dirty eviction and atomic operations preserve coherence", dirtyEvictionFailsWithoutLosingWrites)
        + runTest("disabled and oversized caches fall back to Redis", disabledAndOversizedCachesWriteThrough)
        + runTest("remote collections preserve dirty metadata and recent backend values", remoteCollectionsStayCoherentWithDirtyMetadata);
}
