#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

// Dial addresses are independent of the original storage TLS identity.
// A configured map is an allowlist: an unknown promoted node must not bypass Wall.
struct StorageDialAddress { std::string host; int port; };
inline std::optional<StorageDialAddress> resolveStorageDialAddress(
        const std::string& routes, const std::string& identity, int port) {
    if (routes.empty()) return StorageDialAddress{identity, port};
    try {
        const auto mapping = nlohmann::json::parse(routes);
        if (!mapping.is_object()) return std::nullopt;
        const auto entry = mapping.find(identity + ":" + std::to_string(port));
        if (entry == mapping.end() || !entry->is_object()) return std::nullopt;
        const auto host = entry->at("host").get<std::string>();
        if (!entry->at("port").is_number_integer()) return std::nullopt;
        const auto target_port = entry->at("port").get<std::int64_t>();
        if (host.empty() || target_port < 1 || target_port > 65535) return std::nullopt;
        return StorageDialAddress{host, static_cast<int>(target_port)};
    } catch (...) { return std::nullopt; }
}
