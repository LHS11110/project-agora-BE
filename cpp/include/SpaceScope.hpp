#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>

enum class SpaceScope {
    Canvas,
    Workspace
};

struct SpaceKey {
    SpaceScope scope{SpaceScope::Canvas};
    int id{0};
    bool operator==(const SpaceKey& other) const noexcept {
        return scope == other.scope && id == other.id;
    }
};

namespace std {
template <>
struct hash<SpaceKey> {
    std::size_t operator()(const SpaceKey& key) const noexcept {
        return (static_cast<std::size_t>(key.scope == SpaceScope::Workspace ? 1 : 0) << 32) ^
               static_cast<std::size_t>(static_cast<unsigned int>(key.id));
    }
};
}

inline constexpr std::string_view spaceScopeWire(SpaceScope scope) noexcept {
    return scope == SpaceScope::Workspace ? "workspace" : "canvas";
}

inline constexpr std::string_view spaceScopeIdField(SpaceScope scope) noexcept {
    return scope == SpaceScope::Workspace ? "workspace-id" : "canvas-id";
}

inline constexpr std::string_view spaceScopeNameField(SpaceScope scope) noexcept {
    return scope == SpaceScope::Workspace ? "workspace-name" : "canvas-name";
}

inline constexpr std::string_view spaceScopeJwtSubject(SpaceScope scope) noexcept {
    return scope == SpaceScope::Workspace ? "workspace-access" : "canvas-access";
}

inline constexpr std::string_view spaceScopeJwtIdClaim(SpaceScope scope) noexcept {
    return scope == SpaceScope::Workspace ? "workspaceId" : "canvasId";
}

inline SpaceScope parseSpaceScope(std::string_view raw) {
    if (raw.empty() || raw == "canvas") return SpaceScope::Canvas;
    if (raw == "workspace") return SpaceScope::Workspace;
    throw std::invalid_argument("Unknown space scope");
}

inline int runtimeTargetId(SpaceScope scope, int space_id) {
    if (space_id <= 0 || scope == SpaceScope::Canvas) return space_id;
    constexpr int kWorkspaceRuntimeOffset = 1000000000;
    if (space_id > (std::numeric_limits<int>::max)() - kWorkspaceRuntimeOffset) {
        throw std::invalid_argument("Workspace runtime id overflow");
    }
    return kWorkspaceRuntimeOffset + space_id;
}
