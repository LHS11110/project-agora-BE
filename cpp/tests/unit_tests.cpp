#include "Canvas.hpp"
#include "CanvasPassword.hpp"
#include "SqlCommand.hpp"
#include "TestSupport.hpp"

#include <functional>
#include <string>
#include <vector>

namespace {
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

void canvasTracksMultipleSocketsPerUser() {
    Canvas canvas(73);
    canvas.connectUser(9);
    canvas.connectUser(9);
    canvas.connectUser(10);

    AGORA_CHECK(canvas.isUserActive(9));
    AGORA_CHECK(canvas.user_conn_counts.at(9) == 2);
    AGORA_CHECK(canvas.getActiveUsers().size() == 2);
    AGORA_CHECK(!canvas.disconnectUser(9));
    AGORA_CHECK(canvas.isUserActive(9));
    AGORA_CHECK(canvas.disconnectUser(9));
    AGORA_CHECK(!canvas.isUserActive(9));
    AGORA_CHECK(!canvas.disconnectUser(9));
    AGORA_CHECK(canvas.getActiveUsers().size() == 1);
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
    failures += runTest("password hash format and legacy normalization", passwordHashFormatsAreValidatedAndNormalized);
    failures += runTest("multiple websocket sessions per user", canvasTracksMultipleSocketsPerUser);
    failures += runTest("canvas persistence ordering and barrier", persistenceQueuePreservesOrderAndBarrier);
    failures += runTest("SQL values stay separate from query text", sqlCommandKeepsUntrustedTextOutsideTheStatement);
    return failures == 0 ? 0 : 1;
}
