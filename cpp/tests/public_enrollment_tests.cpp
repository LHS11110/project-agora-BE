#include "service/CanvasPublicEnrollment.hpp"
#include "TestSupport.hpp"
using J = nlohmann::json;
static J canvas(){return {{"visibility","public"},{"people",J::array({1})},{"admin-user-id",1},{"init-group","default"},{"inner-group",{{"admin-group",J::array({1})},{"default",J::array()}}},{"group-permissions",{{"admin-group",7},{"default",7}}}};}
static void enrollment(){
 auto d=canvas();AGORA_CHECK(enrollPublicParticipant(d,2));AGORA_CHECK(d["people"]==J::array({1,2}));AGORA_CHECK(d["inner-group"]["default"]==J::array({2}));AGORA_CHECK(!enrollPublicParticipant(d,2));AGORA_CHECK(d["people"].size()==2);
 auto privateCanvas=canvas();privateCanvas["visibility"]="private";AGORA_CHECK(!enrollPublicParticipant(privateCanvas,2));AGORA_CHECK(privateCanvas["people"].size()==1);
 auto adminGroup=canvas();adminGroup["init-group"]="admin-group";bool badAdmin=false;try{enrollPublicParticipant(adminGroup,2);}catch(const std::invalid_argument&){badAdmin=true;}AGORA_CHECK(badAdmin);AGORA_CHECK(adminGroup["people"].size()==1);
 auto noRead=canvas();noRead["group-permissions"]["default"]=0;bool denied=false;try{enrollPublicParticipant(noRead,2);}catch(const std::invalid_argument&){denied=true;}AGORA_CHECK(denied);AGORA_CHECK(noRead["people"].size()==1);
}
int main(){return runTest("public participant registration, idempotence and group restrictions",enrollment);}
