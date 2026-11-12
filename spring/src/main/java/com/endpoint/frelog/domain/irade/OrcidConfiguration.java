package com.endpoint.frelog.domain.irade;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;
import java.net.URI;

@Component
public record OrcidConfiguration(boolean enabled,String clientId,String clientSecret,String issuer,String redirectUri) {
    public OrcidConfiguration(
            @Value("${app.irade.orcid.enabled:false}") boolean enabled,
            @Value("${app.irade.orcid.client-id:}") String clientId,
            @Value("${app.irade.orcid.client-secret:}") String clientSecret,
            @Value("${app.irade.orcid.issuer:https://orcid.org}") String issuer,
            @Value("${app.irade.orcid.redirect-uri:https://frelog.p-e.kr/api/irade/auth/orcid/callback}") String redirectUri) {
        this.enabled=enabled;this.clientId=clientId;this.clientSecret=clientSecret;this.issuer=issuer;this.redirectUri=redirectUri;
        if(!issuer.equals("https://orcid.org")&&!issuer.equals("https://sandbox.orcid.org"))throw new IllegalArgumentException("Only official ORCID issuers are supported");
        var uri=URI.create(redirectUri);
        if(!"https".equals(uri.getScheme())||uri.getHost()==null||uri.getUserInfo()!=null||uri.getQuery()!=null||uri.getFragment()!=null||!"/api/irade/auth/orcid/callback".equals(uri.getPath()))throw new IllegalArgumentException("ORCID requires a fixed public HTTPS callback");
        if(enabled&&(clientId.isBlank()||clientSecret.isBlank()))throw new IllegalArgumentException("ORCID enabled without client credentials");
    }
    public String origin(){return URI.create(redirectUri).resolve("/").toString().replaceAll("/$","");}
    @Override public String toString(){return "OrcidConfiguration[enabled="+enabled+", issuer="+issuer+"]";}
}
