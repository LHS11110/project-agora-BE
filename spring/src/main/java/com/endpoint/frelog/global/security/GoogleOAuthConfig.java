package com.endpoint.frelog.global.security;

import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.annotation.Order;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configurers.AbstractHttpConfigurer;
import org.springframework.security.config.http.SessionCreationPolicy;
import org.springframework.security.oauth2.client.registration.*;
import org.springframework.security.oauth2.client.web.*;
import org.springframework.security.oauth2.core.AuthorizationGrantType;
import org.springframework.security.oauth2.core.oidc.user.OidcUser;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.security.web.SecurityFilterChain;
import org.springframework.security.web.savedrequest.NullRequestCache;
import java.net.URI;

@Configuration
@ConditionalOnProperty(name = "app.oauth.google.enabled", havingValue = "true")
public class GoogleOAuthConfig {
    public static final String HANDOFF = GoogleOAuthConfig.class.getName() + ".handoff";

    @Bean
    ClientRegistrationRepository googleRegistration(
            @Value("${app.oauth.google.client-id}") String id,
            @Value("${app.oauth.google.client-secret}") String secret,
            @Value("${app.oauth.google.redirect-uri}") String redirect) {
        URI uri = URI.create(redirect);
        if (id.isBlank() || secret.isBlank() || !"https".equals(uri.getScheme()) || uri.getHost() == null
                || uri.getUserInfo() != null || uri.getQuery() != null || uri.getFragment() != null
                || !"/api/auth/oauth2/callback/google".equals(uri.getPath())) {
            throw new IllegalArgumentException("Google OAuth requires credentials and an explicit HTTPS callback URI");
        }
        return new InMemoryClientRegistrationRepository(ClientRegistration.withRegistrationId("google")
                .clientId(id).clientSecret(secret).clientName("Google")
                .authorizationGrantType(AuthorizationGrantType.AUTHORIZATION_CODE)
                .redirectUri(redirect).scope("openid", "email", "profile")
                .authorizationUri("https://accounts.google.com/o/oauth2/v2/auth")
                .tokenUri("https://oauth2.googleapis.com/token")
                .jwkSetUri("https://www.googleapis.com/oauth2/v3/certs")
                .issuerUri("https://accounts.google.com")
                .userInfoUri("https://openidconnect.googleapis.com/v1/userinfo")
                .userNameAttributeName("sub").build());
    }

    @Bean @Order(1)
    SecurityFilterChain googleOAuthChain(HttpSecurity http, ClientRegistrationRepository clients, AuthService auth) throws Exception {
        // Never build browser destinations from an internal listener or forwarded headers.
        String loginPage = URI.create(clients.findByRegistrationId("google").getRedirectUri()).resolve("/login").toString();
        var resolver = new DefaultOAuth2AuthorizationRequestResolver(clients, "/api/auth/oauth2/authorize");
        resolver.setAuthorizationRequestCustomizer(OAuth2AuthorizationRequestCustomizers.withPkce());
        http.securityMatcher("/api/auth/oauth2/**")
                .authorizeHttpRequests(a -> a.anyRequest().permitAll())
                .sessionManagement(s -> s.sessionCreationPolicy(SessionCreationPolicy.IF_REQUIRED))
                .requestCache(c -> c.requestCache(new NullRequestCache()))
                .formLogin(AbstractHttpConfigurer::disable).httpBasic(AbstractHttpConfigurer::disable)
                .oauth2Login(o -> o.authorizationEndpoint(a -> a.authorizationRequestResolver(resolver))
                        .redirectionEndpoint(r -> r.baseUri("/api/auth/oauth2/callback/*"))
                        .successHandler((request, response, authentication) -> {
                            try {
                                var result = auth.loginGoogle((OidcUser) authentication.getPrincipal(), request);
                                var previous = request.getSession(false);
                                if (previous != null) previous.invalidate();
                                SecurityContextHolder.clearContext();
                                var session = request.getSession(true);
                                session.setMaxInactiveInterval(60);
                                session.setAttribute(HANDOFF, result);
                                response.sendRedirect(loginPage + "?oauth=success");
                            } catch (RuntimeException exception) {
                                var session = request.getSession(false);
                                if (session != null) session.invalidate();
                                SecurityContextHolder.clearContext();
                                String reason = exception instanceof CustomException c && c.getErrorCode() == ErrorCode.EMAIL_ALREADY_EXISTS
                                        ? "account_exists" : "failed";
                                response.sendRedirect(loginPage + "?oauth=" + reason);
                            }
                        })
                        .failureHandler((request, response, exception) -> {
                            var session = request.getSession(false);
                            if (session != null) session.invalidate();
                            SecurityContextHolder.clearContext();
                            response.sendRedirect(loginPage + "?oauth=failed");
                        }));
        return http.build();
    }
}
