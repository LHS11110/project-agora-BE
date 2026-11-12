package com.endpoint.frelog.domain.irade;

import org.springframework.stereotype.Component;
import org.springframework.http.*;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.web.client.RestClient;
import org.springframework.util.LinkedMultiValueMap;
import java.net.http.HttpClient;
import java.time.Duration;
import java.util.Map;

@Component
public class OrcidClient {
    private final OrcidConfiguration config;
    private final RestClient client;
    @org.springframework.beans.factory.annotation.Autowired
    public OrcidClient(OrcidConfiguration config){this(config,http());}
    OrcidClient(OrcidConfiguration config,RestClient client){this.config=config;this.client=client;}
    private static RestClient http(){
        var factory=new JdkClientHttpRequestFactory(HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(10)).followRedirects(HttpClient.Redirect.NEVER).build());
        factory.setReadTimeout(Duration.ofSeconds(20));
        return RestClient.builder().requestFactory(factory).build();
    }
    public OrcidIdentity exchange(String code){
        if(code==null||code.isBlank()||code.length()>1024)throw new IllegalArgumentException("Invalid authorization code");
        var body=new LinkedMultiValueMap<String,String>();
        body.add("client_id",config.clientId());body.add("client_secret",config.clientSecret());body.add("grant_type","authorization_code");body.add("code",code);body.add("redirect_uri",config.redirectUri());
        var response=client.post().uri(config.issuer()+"/oauth/token").contentType(MediaType.APPLICATION_FORM_URLENCODED).accept(MediaType.APPLICATION_JSON).body(body).retrieve().body(Map.class);
        if(response==null||!(response.get("access_token") instanceof String token)||token.isBlank()||!"bearer".equalsIgnoreCase(String.valueOf(response.get("token_type")))
                ||!(response.get("scope") instanceof String scope)||!java.util.Arrays.asList(scope.split("\\s+")).contains("/authenticate")||!(response.get("orcid") instanceof String id))throw new IllegalArgumentException("Invalid ORCID token response");
        // The provider access token is not needed for login and is neither persisted nor sent to the browser.
        return new OrcidIdentity(id,response.get("name") instanceof String name?name:null,config.issuer());
    }
}
