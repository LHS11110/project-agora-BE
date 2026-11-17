#include "service/CanvasQueryRules.hpp"
#include "service/CanvasAccessRules.hpp"
#include "service/CanvasQueryService.hpp"
#include "SpaceScope.hpp"
#include "TestSupport.hpp"
void aclAndPatchRulesRejectEscalation() {
    const std::unordered_set<std::string> groups{"writers"};
    const nlohmann::json item{{"kind", "rectangle"}, {"permission", "writers"}, {"x", 1}};
    AGORA_CHECK(queryCanAccess(item, groups, false));
    AGORA_CHECK(!queryCanAccess(item, {"readers"}, false));
    AGORA_CHECK(queryUpdatedItem(item, item, groups, false) == item);
    auto escalated = item; escalated["permission"] = "admin-group";
    bool rejected = false;
    try { queryUpdatedItem(item, escalated, groups, false); } catch (const std::invalid_argument&) { rejected = true; }
    AGORA_CHECK(rejected);
    auto multi_member = item; multi_member["permission"] = nlohmann::json::array({"writers", "research"});
    const std::unordered_set<std::string> member_groups{"writers", "research"};
    AGORA_CHECK(queryUpdatedItem(item, multi_member, groups, false, &member_groups)["permission"] == multi_member["permission"]);
    rejected = false;
    try { queryUpdatedItem(item, multi_member, groups, false); } catch (const std::invalid_argument&) { rejected = true; }
    AGORA_CHECK(rejected);
    auto text = item; text["kind"] = "text";
    rejected = false; try { queryUpdatedItem(item, text, groups, false); } catch (const std::invalid_argument&) { rejected = true; }
    AGORA_CHECK(rejected);
    auto locked = item; locked["locked"] = true;
    auto unlocked = locked; unlocked["locked"] = false;
    AGORA_CHECK(queryUpdatedItem(locked, unlocked, groups, false)["locked"] == false);
    auto changed = unlocked; changed["x"] = 2;
    rejected = false; try { queryUpdatedItem(locked, changed, groups, false); } catch(const std::invalid_argument& e) { rejected = std::string(e.what()) == "ITEM_LOCKED"; } AGORA_CHECK(rejected);
    rejected = false; try { queryUpdatedItem(locked, locked, groups, true); } catch(const std::invalid_argument& e) { rejected = std::string(e.what()) == "ITEM_LOCKED"; } AGORA_CHECK(rejected);
    auto invalid = item; invalid["locked"] = "true";
    rejected = false; try { queryUpdatedItem(item, invalid, groups, false); } catch(const std::invalid_argument& e) { rejected = std::string(e.what()) == "ITEM_LOCK_INVALID"; } AGORA_CHECK(rejected);
    // A locked target keeps comment permissions; comments do not edit its contents.
    AGORA_CHECK(queryCommentPermission(locked, groups, false) == "writers");
    AGORA_CHECK(!validQueryItemId("__proto__") && !validQueryItemId("constructor") && validQueryItemId("escaped\"id"));
}
void canvasVisibilityRules() {
    nlohmann::json doc={{"people",nlohmann::json::array({1})},{"inner-group",{{"admin-group",nlohmann::json::array({1})},{"default",nlohmann::json::array()}}},{"init-group","default"}};
    AGORA_CHECK(!canvasIsPublic(doc) && canvasHasMember(doc,1) && !canvasHasMember(doc,2));
    AGORA_CHECK(groupsForUser(doc,2).first.empty());
    doc["visibility"]="public";
    AGORA_CHECK(canvasIsPublic(doc) && groupsForUser(doc,2).first.count("default") == 1 && !groupsForUser(doc,2).second);
    AGORA_CHECK(groupsForUser(doc,1).second && groupsForUser(doc,1).first.count("admin-group") == 1 && groupsForUser(doc,1).first.count("default") == 0);
    AGORA_CHECK(canvasMemberGroups(doc,1).count("default") == 0);
    doc["inner-group"]["default"].push_back(1);
    AGORA_CHECK(groupsForUser(doc,1).first.count("default") == 1 && canvasMemberGroups(doc,1).count("default") == 1);
    doc["inner-group"]["default"].clear();
    doc["inner-group"]["research"]=nlohmann::json::array({2});
    AGORA_CHECK(groupsForUser(doc,2).first.count("research") == 1 && groupsForUser(doc,2).first.count("default") == 0);
    AGORA_CHECK(canvasMemberGroups(doc,2).count("research") == 1 && canvasMemberGroups(doc,2).count("default") == 0);
    doc["inner-group"].erase("research");
    doc["canvas-password-hash"]="unexpected";
    AGORA_CHECK(canvasIsPublic(doc) && groupsForUser(doc,2).first.count("default") == 1);
    doc["canvas-password-hash"]=nullptr;
    doc["init-group"]="admin-group";
    AGORA_CHECK(groupsForUser(doc,2).first.empty() && !groupsForUser(doc,2).second);
    doc["visibility"]="invalid";
    AGORA_CHECK(!canvasIsPublic(doc));
    doc["visibility"]="private";doc["init-group"]="default";doc["inner-group"]["default"]=nlohmann::json::array({2});
    doc["group-permissions"]={{"default",{{"read",true},{"write",false},{"execute",true}}}};
    AGORA_CHECK(groupsForUser(doc,2).first.count("default")==1);
    AGORA_CHECK(groupsForUser(doc,2,"write").first.empty());
    AGORA_CHECK(groupsForUser(doc,2,"execute").first.count("default")==1);
    AGORA_CHECK(groupsForUser(doc,1,"write").second);
    doc["group-permissions"]["default"]["read"]=false;
    AGORA_CHECK(groupsForUser(doc,2).first.empty()&&groupsForUser(doc,2,"execute").first.empty());
    doc["group-permissions"]["default"]["read"]="true";
    AGORA_CHECK(groupsForUser(doc,2).first.empty());
    doc["group-permissions"].erase("default");
    AGORA_CHECK(groupsForUser(doc,2,"write").first.count("default")==1);

}
void commentsFollowCurrentObjectPermission() {
    nlohmann::json target{{"kind", "shape"}, {"permission", "writers"}};
    AGORA_CHECK(queryCommentPermission(target, {"writers"}, false) == "writers");
    bool denied = false;
    try { queryCommentPermission(target, {"readers"}, false); } catch (const std::invalid_argument&) { denied = true; }
    AGORA_CHECK(denied);
    target["permission"] = "readers";
    denied = false;
    try { queryCommentPermission(target, {"writers"}, false); } catch (const std::invalid_argument&) { denied = true; }
    AGORA_CHECK(denied && queryCommentPermission(target, {"readers"}, false) == "readers");
    target["type"] = "chat_room";
    denied = false;
    try { queryCommentPermission(target, {"readers"}, true); } catch (const std::invalid_argument&) { denied = true; }
    AGORA_CHECK(denied);
    nlohmann::json broadcasts = nlohmann::json::array({{{"event", {{"type", "chat"}}}, {"permission", "readers"}}});
    broadcasts.insert(broadcasts.begin(), queryChatRoomBroadcast("room", "readers", 2));
    AGORA_CHECK(broadcasts.size() == 2 && broadcasts[0].is_object() && broadcasts[0]["event"]["type"] == "item_update");
    AGORA_CHECK(broadcasts[0]["permission"] == "readers" && broadcasts[0]["event"]["item"]["next_sequence"] == 2);
}
void spaceScopeIsolationAndQueryAuth() {
    AGORA_CHECK(spaceScopeWire(SpaceScope::Canvas) == "canvas");
    AGORA_CHECK(spaceScopeWire(SpaceScope::Workspace) == "workspace");
    AGORA_CHECK(spaceScopeIdField(SpaceScope::Workspace) == "workspace-id");
    AGORA_CHECK(spaceScopeNameField(SpaceScope::Workspace) == "workspace-name");
    AGORA_CHECK(spaceScopeJwtSubject(SpaceScope::Canvas) == "canvas-access");
    AGORA_CHECK(spaceScopeJwtSubject(SpaceScope::Workspace) == "workspace-access");
    AGORA_CHECK(spaceScopeJwtIdClaim(SpaceScope::Workspace) == "workspaceId");
    AGORA_CHECK(parseSpaceScope("") == SpaceScope::Canvas && parseSpaceScope("workspace") == SpaceScope::Workspace);
    AGORA_CHECK(runtimeTargetId(SpaceScope::Canvas, 12) == 12);
    AGORA_CHECK(runtimeTargetId(SpaceScope::Workspace, 12) == 1000000012);

    std::unordered_set<SpaceKey> keys;
    keys.insert({SpaceScope::Canvas, 5});
    keys.insert({SpaceScope::Workspace, 5});
    AGORA_CHECK(keys.size() == 2);

    CanvasLifecycleMemory pool;
    CanvasQueryService queries(pool, 8002);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    nlohmann::json valid_identity{
        {"canvas_id", 5},
        {"scope", "canvas"},
        {"connection_id", "conn-1"},
        {"expires_at", now + 60},
        {"user_id", 9},
        {"groups", nlohmann::json::array({"default"})},
        {"is_admin", false}
    };
    auto cross_scope = queries.execute({
        {"command", "query"},
        {"route", {{"canvas_id", 5}, {"connection_id", "conn-1"}, {"request_id", "req-1"}, {"scope", "workspace"}}},
        {"identity", valid_identity},
        {"event", {{"type", "canvas_sync"}, {"client_id", "c1"}}}
    });
    AGORA_CHECK(cross_scope["route"]["scope"] == "workspace");
    AGORA_CHECK(!cross_scope["response"]["ok"].get<bool>() && cross_scope["response"]["code"] == "QUERY_UNAUTHORIZED");

    auto invalid_scope = queries.execute({
        {"command", "query"},
        {"route", {{"canvas_id", 5}, {"connection_id", "conn-1"}, {"request_id", "req-2"}, {"scope", "other"}}}
    });
    AGORA_CHECK(!invalid_scope["response"]["ok"].get<bool>() && invalid_scope["response"]["code"] == "ROUTE_INVALID");

    auto mismatched_connect = queries.execute({
        {"command", "connect"},
        {"ws_port", 8002},
        {"route", {{"canvas_id", 5}, {"connection_id", "conn-2"}, {"request_id", "req-3"}, {"scope", "workspace"}}},
        {"principal", {{"canvas_id", 5}, {"scope", "canvas"}, {"nickname", "tester"}, {"tag_number", 1}, {"settings_revision", 0}}}
    });
    AGORA_CHECK(!mismatched_connect["response"]["ok"].get<bool>() && mismatched_connect["response"]["code"] == "CANVAS_ACCESS_UNAUTHORIZED");

    valid_identity["scope"] = "workspace";
    auto ws_query = queries.execute({
        {"command", "query"},
        {"route", {{"canvas_id", 5}, {"connection_id", "conn-1"}, {"request_id", "req-4"}, {"scope", "workspace"}}},
        {"identity", valid_identity},
        {"event", {{"type", "canvas_sync"}, {"client_id", "c1"}}}
    });
    AGORA_CHECK(!ws_query["response"]["ok"].get<bool>() && ws_query["response"]["code"] == "CANVAS_NOT_READY");
}
int main() {
    int a = runTest("query ACL, collaborative payload and ID validation", aclAndPatchRulesRejectEscalation);
    if (!a) a = runTest("canvas visibility and public group rules", canvasVisibilityRules);
    if (!a) a = runTest("comment object permission", commentsFollowCurrentObjectPermission);
    return a ? a : runTest("space scope isolation and query auth", spaceScopeIsolationAndQueryAuth);
}
