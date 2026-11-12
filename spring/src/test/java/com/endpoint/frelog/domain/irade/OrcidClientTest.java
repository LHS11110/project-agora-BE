package com.endpoint.frelog.domain.irade;
import org.junit.jupiter.api.Test;
import org.springframework.web.client.RestClient;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.http.*;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.*;
import static org.springframework.test.web.client.response.MockRestResponseCreators.*;
import static org.assertj.core.api.Assertions.*;
import static org.hamcrest.Matchers.containsString;
class OrcidClientTest {
    @Test void exchangesOnlyServerSideAndValidatesAuthenticatedId(){
        var builder=RestClient.builder();var server=MockRestServiceServer.bindTo(builder).build();
        var config=new OrcidConfiguration(true,"APP-test","private-test-secret","https://orcid.org","https://frelog.p-e.kr/api/irade/auth/orcid/callback");
        var client=new OrcidClient(config,builder.build());
        server.expect(requestTo("https://orcid.org/oauth/token")).andExpect(method(HttpMethod.POST))
            .andExpect(header(HttpHeaders.ACCEPT,"application/json")).andExpect(content().string(containsString("grant_type=authorization_code")))
            .andExpect(content().string(containsString("client_secret=private-test-secret"))).andExpect(content().string(containsString("redirect_uri=https%3A%2F%2Ffrelog.p-e.kr%2Fapi%2Firade%2Fauth%2Forcid%2Fcallback")))
            .andRespond(withSuccess("{\"access_token\":\"provider-secret\",\"token_type\":\"bearer\",\"scope\":\"/authenticate\",\"orcid\":\"0000-0002-1825-0097\",\"name\":\"Researcher\"}",MediaType.APPLICATION_JSON));
        assertThat(client.exchange("valid-code")).isEqualTo(new OrcidIdentity("0000-0002-1825-0097","Researcher","https://orcid.org"));server.verify();
    }
    @Test void rejectsMissingAuthenticationScopeAndInvalidIdentifier(){
        for(String response:new String[]{"{\"access_token\":\"token\",\"token_type\":\"bearer\",\"scope\":\"/read-public\",\"orcid\":\"0000-0002-1825-0097\"}","{\"access_token\":\"token\",\"token_type\":\"bearer\",\"scope\":\"/authenticate\",\"orcid\":\"0000-0002-1825-0098\"}"}){
            var builder=RestClient.builder();var server=MockRestServiceServer.bindTo(builder).build();
            server.expect(requestTo("https://orcid.org/oauth/token")).andRespond(withSuccess(response,MediaType.APPLICATION_JSON));
            var client=new OrcidClient(new OrcidConfiguration(true,"id","secret","https://orcid.org","https://frelog.p-e.kr/api/irade/auth/orcid/callback"),builder.build());
            assertThatThrownBy(()->client.exchange("valid-code")).isInstanceOf(IllegalArgumentException.class);server.verify();
        }
    }
}
