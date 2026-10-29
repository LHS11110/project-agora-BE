#include "service/CanvasQueryRules.hpp"
#include "service/CanvasAccessRules.hpp"
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
    auto text = item; text["kind"] = "text";
    rejected = false; try { queryUpdatedItem(item, text, groups, false); } catch (const std::invalid_argument&) { rejected = true; }
    AGORA_CHECK(rejected);
    AGORA_CHECK(!validQueryItemId("__proto__") && !validQueryItemId("constructor") && validQueryItemId("escaped\"id"));
}
void canvasVisibilityRules() {
    nlohmann::json doc={{"people",nlohmann::json::array({1})},{"inner-group",{{"admin-group",nlohmann::json::array({1})},{"default",nlohmann::json::array()}}},{"init-group","default"}};
    AGORA_CHECK(!canvasIsPublic(doc) && canvasHasMember(doc,1) && !canvasHasMember(doc,2));
    AGORA_CHECK(groupsForUser(doc,2).first.empty());
    doc["visibility"]="public";
    AGORA_CHECK(canvasIsPublic(doc) && groupsForUser(doc,2).first.count("default") == 1 && !groupsForUser(doc,2).second);
    doc["canvas-password-hash"]="unexpected";
    AGORA_CHECK(!canvasIsPublic(doc) && groupsForUser(doc,2).first.empty());
    doc["canvas-password-hash"]=nullptr;
    doc["init-group"]="admin-group";
    AGORA_CHECK(groupsForUser(doc,2).first.empty() && !groupsForUser(doc,2).second);
    doc["visibility"]="invalid";
    AGORA_CHECK(!canvasIsPublic(doc));
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
int main() { int a=runTest("query ACL, collaborative payload and ID validation", aclAndPatchRulesRejectEscalation); if(a)return a; a=runTest("canvas visibility and public group rules", canvasVisibilityRules); return a ? a : runTest("comment object permission", commentsFollowCurrentObjectPermission); }
