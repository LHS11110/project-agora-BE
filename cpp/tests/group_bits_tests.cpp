#include "service/CanvasGroupBits.hpp"
#include "service/CanvasQueryRules.hpp"
#include "service/CanvasGroupManagement.hpp"
#include "TestSupport.hpp"
using J=nlohmann::json;
void bits(){
 J doc{{"inner-group",J::object()},{"group-permissions",J::object()}};
 for(unsigned i=0;i<64;i++){auto name="g"+std::to_string(i);doc["inner-group"][name]=J::array({1});doc["group-bits"][name]=i;doc["group-permissions"][name]=i==63?5:3;}
 const auto layout=canvasGroupBitLayout(doc);CanvasGroupBitIndex index(layout);
 AGORA_CHECK(index.valid&&index.bit("g63")==UINT64_C(0x8000000000000000));
 AGORA_CHECK(canvasMaskHex(index.mask(J::array({"g63"})))=="8000000000000000");
 AGORA_CHECK(index.mask(J::array({"g0","g63"}))==UINT64_C(0x8000000000000001));
 AGORA_CHECK(canvasStoredGroupBits(doc,"g63")==5&&canvasGroupCapability(doc,"g63","read")&&canvasGroupCapability(doc,"g63","execute")&&!canvasGroupCapability(doc,"g63","write"));
 const std::unordered_set<std::string> readable{"g63"},writable{"g0"};
 {CanvasAclScope scope(layout,{&readable,&writable});AGORA_CHECK(queryCanAccess(J{{"permission",J::array({"g63"})}},readable,false));AGORA_CHECK(!queryCanAccess(J{{"permission","g0"}},readable,false));AGORA_CHECK(!queryCanAccess(J{{"permission",J{{"g63",false}}}},readable,false));AGORA_CHECK(!queryCanAccess(J{{"permission","missing"}},readable,false));}
 AGORA_CHECK(currentCanvasAcl==nullptr);
 bool limit=false;try{applyCanvasGroupManagement(doc,"group_create",J{{"name","g64"}},[](const J&)->std::optional<int>{return 1;});}catch(const std::invalid_argument& e){limit=std::string(e.what())=="GROUP_LIMIT";}AGORA_CHECK(limit&&doc["inner-group"].size()==64);
 auto invalid=layout;invalid["g63"]=64;AGORA_CHECK(!CanvasGroupBitIndex(invalid).valid);invalid["g63"]=0;AGORA_CHECK(!CanvasGroupBitIndex(invalid).valid);
 applyCanvasGroupManagement(doc,"group_rename",J{{"name","g63"},{"new_name","renamed"}},[](const J&)->std::optional<int>{return 1;});AGORA_CHECK(canvasGroupBitLayout(doc)["renamed"]==63);
 applyCanvasGroupManagement(doc,"group_delete",J{{"name","renamed"}},[](const J&)->std::optional<int>{return 1;});
 applyCanvasGroupManagement(doc,"group_create",J{{"name","replacement"},{"permissions",1}},[](const J&)->std::optional<int>{return 1;});AGORA_CHECK(canvasGroupBitLayout(doc)["replacement"]==63);
 AGORA_CHECK(canvasPolicyInputBits(7)==7&&canvasPolicyInputBits(0)==0&&!canvasPolicyInputBits(2)&&!canvasPolicyInputBits(8));
}
int main(){return runTest("64 group bits, bit 63, three-bit capabilities, limits, rename and slot reuse",bits);}
