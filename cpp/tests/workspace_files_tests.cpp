#include "service/CanvasWorkspaceFiles.hpp"
#include "TestSupport.hpp"

void workspacePathsAndRequests() {
    using namespace agora::workspace;
    AGORA_CHECK(path("/") && path("/src/정리.lean") && path("/a b/proof.v"));
    for (const char* invalid : {"", "a", "//a", "/a/", "/../a", "/a/./b", "/a\\b"}) AGORA_CHECK(!path(invalid));
    Json request{{"type", "workspace_files"}, {"request_id", "mutation-1"}, {"operation", "read"}, {"path", "/main.lean"}};
    validate(request);
    for (const auto& entry : std::vector<std::pair<std::string, Json>>{
            {"path", "/../../etc/passwd"}, {"operation", "execute"}, {"userId", 99}, {"canvasId", 2},
            {"document", Json::array()}, {"recursive", "yes"}, {"cursor", -1}, {"request_id", ""}}) {
        auto bad = request; bad[entry.first] = entry.second;
        bool rejected = false;
        try { validate(bad); } catch (const std::invalid_argument&) { rejected = true; }
        AGORA_CHECK(rejected);
    }
    AGORA_CHECK(readOnly("read") && readOnly("stat") && !readOnly("delete") && !readOnly("write"));
}

int main() { return runTest("workspace paths and protocol reject traversal and caller identity", workspacePathsAndRequests); }
