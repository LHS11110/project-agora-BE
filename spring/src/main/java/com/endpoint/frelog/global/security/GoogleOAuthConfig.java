package com.endpoint.frelog.global.security;

import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.auth.oauth.*;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.boot.autoconfigure.condition.ConditionalOnExpression;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.annotation.Order;
import org.springframework.core.env.Environment;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configurers.AbstractHttpConfigurer;
import org.springframework.security.config.http.SessionCreationPolicy;
import org.springframework.security.oauth2.client.registration.*;
import org.springframework.security.oauth2.client.web.*;
import org.springframework.security.oauth2.client.endpoint.RestClientAuthorizationCodeTokenResponseClient;
import org.springframework.security.oauth2.client.userinfo.DefaultOAuth2UserService;
import org.springframework.security.oauth2.client.authentication.OAuth2AuthenticationToken;
import org.springframework.security.oauth2.core.*;
import org.springframework.security.oauth2.core.oidc.user.OidcUser;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.security.web.SecurityFilterChain;
import org.springframework.security.web.savedrequest.NullRequestCache;
import org.springframework.util.LinkedMultiValueMap;
import java.net.URI;
import java.time.Instant;
import java.security.SecureRandom;
import java.util.*;

/** Shared OAuth chain; the class/bean names remain stable for existing integrations. */
@Configuration
@ConditionalOnExpression("${app.oauth.google.enabled:false} or ${app.oauth.naver.enabled:false} or ${app.oauth.kakao.enabled:false}")
public class GoogleOAuthConfig {
    public static final String HANDOFF=GoogleOAuthConfig.class.getName()+".handoff";
    public static final String PENDING=GoogleOAuthConfig.class.getName()+".pending";
    public record EnrollmentResponse(boolean oauthEnrollmentRequired,String provider,String email,String enrollmentToken){}

    @Bean ClientRegistrationRepository googleRegistration(Environment env){
        var clients=new ArrayList<ClientRegistration>();String origin=null;
        for(String provider:List.of("google","naver","kakao")){
            String prefix="app.oauth."+provider+".";
            if(!env.getProperty(prefix+"enabled",Boolean.class,false))continue;
            String id=env.getProperty(prefix+"client-id",""),secret=env.getProperty(prefix+"client-secret",""),redirect=env.getProperty(prefix+"redirect-uri","");
            URI uri=URI.create(redirect);
            if(id.isBlank()||secret.isBlank()||!"https".equals(uri.getScheme())||uri.getHost()==null||uri.getUserInfo()!=null||uri.getQuery()!=null||uri.getFragment()!=null||!("/api/auth/oauth2/callback/"+provider).equals(uri.getPath()))
                throw new IllegalArgumentException(provider+" OAuth requires credentials and an explicit HTTPS callback URI");
            String next=uri.resolve("/").toString();if(origin!=null&&!origin.equals(next))throw new IllegalArgumentException("OAuth callbacks must share the public HTTPS origin");origin=next;
            var b=ClientRegistration.withRegistrationId(provider).clientId(id).clientSecret(secret).clientName(provider)
                    .authorizationGrantType(AuthorizationGrantType.AUTHORIZATION_CODE).redirectUri(redirect)
                    .clientSettings(ClientRegistration.ClientSettings.builder().requireProofKey(!provider.equals("naver")).build());
            if(provider.equals("google"))b.clientName("Google").scope("openid","email","profile")
                    .authorizationUri("https://accounts.google.com/o/oauth2/v2/auth").tokenUri("https://oauth2.googleapis.com/token")
                    .jwkSetUri("https://www.googleapis.com/oauth2/v3/certs").issuerUri("https://accounts.google.com")
                    .userInfoUri("https://openidconnect.googleapis.com/v1/userinfo").userNameAttributeName("sub");
            else if(provider.equals("naver"))b.clientName("네이버").clientAuthenticationMethod(ClientAuthenticationMethod.CLIENT_SECRET_POST)
                    .authorizationUri("https://nid.naver.com/oauth2.0/authorize").tokenUri("https://nid.naver.com/oauth2.0/token")
                    .userInfoUri("https://openapi.naver.com/v1/nid/me").userNameAttributeName("id");
            else b.clientName("카카오").clientAuthenticationMethod(ClientAuthenticationMethod.CLIENT_SECRET_POST)
                    .authorizationUri("https://kauth.kakao.com/oauth/authorize").tokenUri("https://kauth.kakao.com/oauth/token")
                    .userInfoUri("https://kapi.kakao.com/v2/user/me").userNameAttributeName("id");
            clients.add(b.build());
        }
        return new InMemoryClientRegistrationRepository(clients);
    }

    static DefaultOAuth2UserService socialUserService(){
        var service=new DefaultOAuth2UserService();
        service.setAttributesConverter(request->attributes->{
            if("naver".equals(request.getClientRegistration().getRegistrationId())){
                if(!"00".equals(attributes.get("resultcode"))||!(attributes.get("response") instanceof Map<?,?> profile)||!(profile.get("id") instanceof String id)||id.isBlank())
                    throw new OAuth2AuthenticationException(new OAuth2Error("invalid_user_info"));
                var flattened=new LinkedHashMap<>(attributes);flattened.put("id",id);return flattened;
            }
            return attributes;
        });return service;
    }

    static RestClientAuthorizationCodeTokenResponseClient tokenClient(){
        var client=new RestClientAuthorizationCodeTokenResponseClient();
        client.addParametersConverter(request->{
            var parameters=new LinkedMultiValueMap<String,String>();
            if("naver".equals(request.getClientRegistration().getRegistrationId()))parameters.add("state",request.getAuthorizationExchange().getAuthorizationRequest().getState());
            return parameters;
        });return client;
    }

    @Bean @Order(1)
    SecurityFilterChain googleOAuthChain(HttpSecurity http,ClientRegistrationRepository clients,AuthService auth) throws Exception {
        var registrations=(Iterable<ClientRegistration>)clients;
        String loginPage=URI.create(registrations.iterator().next().getRedirectUri()).resolve("/login").toString();
        String publicOrigin=URI.create(loginPage).resolve("/").toString().replaceAll("/$","");
        var resolver=new DefaultOAuth2AuthorizationRequestResolver(clients,"/api/auth/oauth2/authorize");
        // Completion APIs use the stateless JWT chain for existing-account reauthentication.
        http.securityMatcher(request->{
                    String path=request.getRequestURI().substring(request.getContextPath().length());
                    return path.startsWith("/api/auth/oauth2/")&&!Set.of("/api/auth/oauth2/enrollment","/api/auth/oauth2/email/request").contains(path);
                })
                .authorizeHttpRequests(a->a.anyRequest().permitAll())
                .sessionManagement(s->s.sessionCreationPolicy(SessionCreationPolicy.IF_REQUIRED))
                .requestCache(c->c.requestCache(new NullRequestCache()))
                .formLogin(AbstractHttpConfigurer::disable).httpBasic(AbstractHttpConfigurer::disable)
                .oauth2Login(o->o.authorizationEndpoint(a->a.authorizationRequestResolver(resolver))
                        .redirectionEndpoint(r->r.baseUri("/api/auth/oauth2/callback/*"))
                        .tokenEndpoint(t->t.accessTokenResponseClient(tokenClient()))
                        .userInfoEndpoint(u->u.userService(socialUserService()))
                        .successHandler((request,response,authentication)->{
                            try{
                                var oauth=(OAuth2AuthenticationToken)authentication;
                                String provider=oauth.getAuthorizedClientRegistrationId();
                                SocialIdentity identity=SocialIdentity.from(provider,oauth.getPrincipal());
                                Object result;
                                try{result=provider.equals("google")?auth.loginGoogle((OidcUser)oauth.getPrincipal(),request):auth.loginSocial(identity,request);}
                                catch(OAuthEmailRequired required){
                                    byte[] nonce=new byte[32];new SecureRandom().nextBytes(nonce);
                                    String token=Base64.getUrlEncoder().withoutPadding().encodeToString(nonce);
                                    result=new PendingOAuthEnrollment(identity,token,publicOrigin,Instant.now().plusSeconds(300));
                                }
                                var previous=request.getSession(false);if(previous!=null)previous.invalidate();SecurityContextHolder.clearContext();
                                var session=request.getSession(true);
                                if(result instanceof PendingOAuthEnrollment pending){
                                    session.setMaxInactiveInterval(300);session.setAttribute(PENDING,pending);
                                    session.setAttribute(HANDOFF,new EnrollmentResponse(true,provider,identity.email()==null?"":identity.email(),pending.token()));
                                }else{session.setMaxInactiveInterval(60);session.setAttribute(HANDOFF,result);}
                                response.sendRedirect(loginPage+"?oauth=success");
                            }catch(RuntimeException exception){
                                var session=request.getSession(false);if(session!=null)session.invalidate();SecurityContextHolder.clearContext();
                                String reason=exception instanceof CustomException c&&c.getErrorCode()==ErrorCode.EMAIL_ALREADY_EXISTS?"account_exists":"failed";
                                response.sendRedirect(loginPage+"?oauth="+reason);
                            }
                        })
                        .failureHandler((request,response,exception)->{
                            var session=request.getSession(false);if(session!=null)session.invalidate();SecurityContextHolder.clearContext();response.sendRedirect(loginPage+"?oauth=failed");
                        }));
        return http.build();
    }
}
