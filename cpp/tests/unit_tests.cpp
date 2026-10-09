#include "Canvas.hpp"
#include "CanvasPassword.hpp"
#include "memory/SqlCommand.hpp"
#include "memory/StorageEndpointRoutes.hpp"
#include "TestSupport.hpp"

#include <functional>
#include <string>
#include <vector>

namespace {
void brokerRoutesDoNotFallBackToDirectStorage() {
    const std::string routes = R"({"172.20.0.7:6379":{"host":"agora-storage-broker","port":16380}})";
    auto dial = resolveStorageDialAddress(routes, "172.20.0.7", 6379);
    AGORA_CHECK(dial && dial->host == "agora-storage-broker" && dial->port == 16380);
    AGORA_CHECK(!resolveStorageDialAddress(routes, "172.20.0.9", 6379));
    AGORA_CHECK(!resolveStorageDialAddress("invalid", "172.20.0.7", 6379));
    AGORA_CHECK(!resolveStorageDialAddress(R"({"node:6379":{"host":"wall","port":1.5}})", "node", 6379));
    AGORA_CHECK(!resolveStorageDialAddress(R"({"node:6379":{"host":"wall","port":4294967297}})", "node", 6379));
}

void passwordHashFormatsAreValidatedAndNormalized() {
    const std::string encoded =
            "pbkdf2$310000$0123456789abcdef0123456789abcdef$"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    AGORA_CHECK(isCanvasPasswordHash(encoded));
    AGORA_CHECK(normalizeCanvasPassword(encoded) == encoded);
    AGORA_CHECK(!isCanvasPasswordHash("pbkdf2$310000$short$bad"));
    AGORA_CHECK(!hashCanvasPassword("").has_value());

    const auto normalized = normalizeCanvasPassword("legacy-password");
    AGORA_CHECK(normalized.has_value());
    AGORA_CHECK(normalized->rfind("pbkdf2$310000$", 0) == 0);
    AGORA_CHECK(isCanvasPasswordHash(*normalized));
    AGORA_CHECK(normalizeCanvasPassword("") == std::string{});
}

void persistenceQueuePreservesOrderAndBarrier() {
    Canvas canvas(74);
    bool start_worker = false;
    AGORA_CHECK(canvas.enqueuePersistence({{"sequence", 1}}, start_worker));
    AGORA_CHECK(start_worker);
    AGORA_CHECK(canvas.enqueuePersistence({{"sequence", 2}}, start_worker));
    AGORA_CHECK(!start_worker);

    const auto barrier = canvas.persistenceBarrier();
    AGORA_CHECK(barrier == 2);
    nlohmann::json event;
    std::uint64_t ticket = 0;
    AGORA_CHECK(canvas.nextPersistence(event, ticket));
    AGORA_CHECK(ticket == 1 && event["sequence"] == 1);
    canvas.endPersistence(ticket, true);
    AGORA_CHECK(canvas.nextPersistence(event, ticket));
    AGORA_CHECK(ticket == 2 && event["sequence"] == 2);
    canvas.endPersistence(ticket, true);
    canvas.waitForPersistenceThrough(barrier);

    std::unique_lock<std::mutex> settings_lock(canvas.settings_mutex);
    AGORA_CHECK(canvas.waitForPendingPersistence(settings_lock));
    AGORA_CHECK(!canvas.enqueuePersistence({{"sequence", 3}}, start_worker));
}

void persistenceQueueRejectsOverloadWithoutLosingAcceptedWork() {
    Canvas canvas(75);
    bool start = false;
    for (int i = 0; i < 1024; ++i)
        AGORA_CHECK(canvas.enqueuePersistence({{"sequence", i}}, start));
    const auto barrier = canvas.persistenceBarrier();
    AGORA_CHECK(!canvas.enqueuePersistence({{"sequence", 1024}}, start));
    AGORA_CHECK(!start && canvas.persistenceBarrier() == barrier);
    nlohmann::json event; std::uint64_t ticket = 0;
    AGORA_CHECK(canvas.nextPersistence(event, ticket));
    AGORA_CHECK(event["sequence"] == 0);
    canvas.endPersistence(ticket, true);
    AGORA_CHECK(canvas.enqueuePersistence({{"sequence", 1024}}, start));
    for (int i = 1; i <= 1024; ++i) {
        AGORA_CHECK(canvas.nextPersistence(event, ticket));
        AGORA_CHECK(event["sequence"] == i);
        canvas.endPersistence(ticket, true);
    }
    AGORA_CHECK(!canvas.nextPersistence(event, ticket));
}

void sqlCommandKeepsUntrustedTextOutsideTheStatement() {
    const std::string payload = "x'; DROP TABLE cpp_server;--";
    SqlCommand command("SELECT user_id FROM users WHERE nickname = @nickname AND tag_number = @tag;");
    command.addText("@nickname", payload).addInt("@tag", 23);

    AGORA_CHECK(command.isValid());
    AGORA_CHECK(command.statement().find(payload) == std::string::npos);
    AGORA_CHECK(command.parameters().size() == 2);
    AGORA_CHECK(command.parameters()[0].text_value == payload);
    AGORA_CHECK(command.parameterDeclarations() == "@nickname NVARCHAR(4000), @tag INT");

    SqlCommand networkAddress("SELECT server_id FROM cpp_server WHERE server_ip = @ip;");
    networkAddress.addVarchar("@ip", payload);
    AGORA_CHECK(networkAddress.isValid());
    AGORA_CHECK(networkAddress.statement().find(payload) == std::string::npos);
    AGORA_CHECK(networkAddress.parameters()[0].text_value == payload);
    AGORA_CHECK(networkAddress.parameterDeclarations() == "@ip VARCHAR(4000)");

    SqlCommand invalid("SELECT 1;");
    invalid.addText("@value); DROP TABLE users;--", payload);
    AGORA_CHECK(!invalid.isValid());
}
}

int main() {
    int failures = 0;
    failures += runTest("storage routes fail closed for unknown nodes", brokerRoutesDoNotFallBackToDirectStorage);
    failures += runTest("password hash format and legacy normalization", passwordHashFormatsAreValidatedAndNormalized);

    failures += runTest("canvas persistence ordering and barrier", persistenceQueuePreservesOrderAndBarrier);
    failures += runTest("persistence queue overload and recovery", persistenceQueueRejectsOverloadWithoutLosingAcceptedWork);
    failures += runTest("SQL values stay separate from query text", sqlCommandKeepsUntrustedTextOutsideTheStatement);
    return failures == 0 ? 0 : 1;
}
