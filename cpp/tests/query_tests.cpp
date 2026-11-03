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
    doc["canvas-password-hash"]="unexpected";
    AGORA_CHECK(!canvasIsPublic(doc) && groupsForUser(doc,2).first.empty());
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
int main() { int a=runTest("query ACL, collaborative payload and ID validation", aclAndPatchRulesRejectEscalation); if(a)return a; a=runTest("canvas visibility and public group rules", canvasVisibilityRules); return a ? a : runTest("comment object permission", commentsFollowCurrentObjectPermission); }
