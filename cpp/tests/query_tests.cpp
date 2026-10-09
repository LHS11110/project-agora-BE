#include "service/CanvasQueryRules.hpp"
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
int main() { return runTest("query ACL, collaborative payload and ID validation", aclAndPatchRulesRejectEscalation); }
