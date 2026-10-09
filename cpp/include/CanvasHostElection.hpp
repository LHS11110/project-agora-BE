#pragma once
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

struct CanvasHostCandidate {
    std::string peer_id;
    bool is_admin{false};
    bool host_protocol{false};
    std::unordered_set<std::string> groups;
};

// Bully priority with eligibility: a host must be authorized for all active groups.
inline std::string electCanvasHost(const std::vector<CanvasHostCandidate>& candidates) {
    std::unordered_set<std::string> required;
    for (const auto& candidate : candidates) {
        if (!candidate.host_protocol) return {}; // Never mix optimistic and hosted clients.
        required.insert(candidate.groups.begin(), candidate.groups.end());
    }
    std::string elected;
    for (const auto& candidate : candidates) {
        const bool covers_groups = candidate.is_admin || std::all_of(required.begin(), required.end(),
            [&](const auto& group) { return candidate.groups.count(group) != 0; });
        if (covers_groups && candidate.peer_id > elected) elected = candidate.peer_id;
    }
    return elected;
}

inline bool canvasHostMayPersist(const std::string& host_peer, const std::string& term,
                                const std::string& sender_peer, const std::string& supplied_term) {
    return !host_peer.empty() && !term.empty() && host_peer == sender_peer && term == supplied_term;
}
