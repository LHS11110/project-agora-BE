#include "service/CanvasMentions.hpp"
#include "TestSupport.hpp"
void mentions() {
 const auto parsed=parseMentionHandles("hello @test user#12! @한글 이름#3 @test user#12");
 AGORA_CHECK(parsed.size()==2 && parsed[0].first=="test user" && parsed[0].second==12 && parsed[1].first=="한글 이름");
 AGORA_CHECK(parseMentionHandles("mail@test#1 @test#123abc @test#99999999999999999").empty());
 AGORA_CHECK(parseMentionHandles("(@a#0), @name#tag#4.").size()==2);
}
int main(){return runTest("canonical mention handles, full tag and boundaries",mentions);}
