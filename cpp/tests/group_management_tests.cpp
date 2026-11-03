#include "service/CanvasGroupManagement.hpp"
#include "TestSupport.hpp"
using J=nlohmann::json;
void test(){
 J doc=J::parse(R"({"people":[1,2],"admin-user-id":1,"inner-group":{"admin-group":[1],"default":[1,2]},"init-group":"default","group-permissions":{"default":{"read":true,"write":true,"execute":true}},"items":{"a":{"kind":"shape","permission":"default"},"b":{"permission":["default","other"]},"c":{"permission":{"default":true}}}})");
 auto resolve=[](const J& p)->std::optional<int>{return p.value("nickname","")=="owner"?1:p.value("nickname","")=="viewer"?2:std::optional<int>{};};
 auto apply=[&](std::string action,J value){return applyCanvasGroupManagement(doc,action,value,resolve);};
 auto deny=[&](std::string action,J value){bool rejected=false;try{apply(action,value);}catch(const std::invalid_argument&){rejected=true;}AGORA_CHECK(rejected);};
 apply("participant_groups",{{"nickname","viewer"},{"tag_number",1},{"groups",J::array({"default"})}});AGORA_CHECK(doc["inner-group"]["default"]==J::array({1,2}));
 deny("participant_groups",{{"nickname","owner"},{"tag_number",1},{"groups",J::array({"default"})}});
 deny("participant_groups",{{"nickname","viewer"},{"tag_number",1},{"groups",J::array({"missing"})}});
 deny("participant_groups",{{"nickname","outsider"},{"groups",J::array({"default"})}});
 apply("group_create",{{"name","research"}});AGORA_CHECK(doc["group-permissions"]["research"]==1);
 deny("group_create",{{"name","research"}});deny("group_create",{{"name","a.b"}});
 apply("group_members",{{"name","research"},{"members",J::array({{{"nickname","viewer"},{"tag_number",1}}})}});AGORA_CHECK(doc["inner-group"]["research"]==J::array({2}));
 deny("group_members",{{"name","admin-group"},{"members",J::array()}});deny("group_members",{{"name","research"},{"members",J::array({{{"nickname","outsider"}}})}});
 deny("group_delete",{{"name","default"}});deny("group_rename",{{"name","admin-group"},{"new_name","admins"}});
 apply("group_rename",{{"name","default"},{"new_name","writers"}});AGORA_CHECK(doc["init-group"]=="writers"&&doc["items"]["a"]["permission"]=="writers"&&doc["items"]["b"]["permission"][0]=="writers"&&doc["items"]["c"]["permission"].contains("writers"));
 apply("group_initial",{{"name","research"}});deny("group_delete",{{"name","writers"}});
 apply("group_create",{{"name","unused"}});apply("group_delete",{{"name","unused"}});AGORA_CHECK(!doc["inner-group"].contains("unused"));
 apply("participant_groups",{{"nickname","viewer"},{"tag_number",1},{"groups",J::array({"research","writers"})}});
 AGORA_CHECK(doc["inner-group"]["research"]==J::array({2})&&doc["inner-group"]["writers"]==J::array({1,2}));
 apply("participant_groups",{{"nickname","viewer"},{"tag_number",1},{"groups",J::array()}});AGORA_CHECK(doc["inner-group"]["research"].empty()&&doc["inner-group"]["writers"]==J::array({1}));
 deny("group_initial",{{"name","admin-group"}});
}
int main(){return runTest("canvas group lifecycle, membership, owner safety and ACL rename",test);}
