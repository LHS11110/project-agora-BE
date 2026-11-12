package com.endpoint.frelog.domain.irade;

import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.service.*;
import com.endpoint.frelog.domain.canvas.dto.*;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.*;
import com.endpoint.frelog.global.security.*;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;
import jakarta.servlet.http.Cookie;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.bean.override.convention.TestBean;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.*;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.WebApplicationContext;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.springframework.security.test.web.servlet.setup.SecurityMockMvcConfigurers.springSecurity;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;
import java.util.*;
import java.time.LocalDateTime;

@SpringBootTest(properties={"app.security.cors.allowed-origins=https://frelog.p-e.kr","app.irade.orcid.enabled=true","app.irade.orcid.client-id=APP-TEST","app.irade.orcid.client-secret=never-expose-test-secret","app.irade.orcid.redirect-uri=https://frelog.p-e.kr/api/irade/auth/orcid/callback"})
class OrcidIntegrationTest {
    @MockitoBean OrcidClient provider;
    @MockitoBean CppServerClient cpp;
    @MockitoBean com.endpoint.frelog.domain.canvas.client.CppRuntimeClient runtime;
    @MockitoBean CanvasRedisDocumentReader redis;
    @MockitoBean CanvasService canvases;
    @MockitoBean com.endpoint.frelog.domain.user.service.UserProfileImageService images;
    @Autowired com.endpoint.frelog.domain.auth.service.AuthService auth;
    @TestBean(methodName="database") DataSource dataSource;
    static DataSource database(){var db=new HikariDataSource();db.setJdbcUrl("jdbc:h2:mem:irade_orcid;MODE=MSSQLServer;DB_CLOSE_DELAY=-1");db.setUsername("sa");return db;}
    @Autowired WebApplicationContext context;
    @Autowired OrcidAttemptRepository attempts;
    @Autowired UserRepository users;
    @Autowired UserSessionRepository sessions;
    @Autowired JwtTokenProvider tokens;
    @Autowired tools.jackson.databind.ObjectMapper json;
    MockMvc mvc;
    static final String ORIGIN="https://frelog.p-e.kr",ID="0000-0002-1825-0097";
    @BeforeEach void setup(){sessions.deleteAll();attempts.deleteAll();users.deleteAll();mvc=MockMvcBuilders.webAppContextSetup(context).apply(springSecurity()).build();when(provider.exchange("valid-code")).thenReturn(new OrcidIdentity(ID,"Test Researcher","https://orcid.org"));}
    record Flow(String state,Cookie browser){}
    Flow begin() throws Exception {
        var result=mvc.perform(get("/api/irade/auth/orcid/start").secure(true).param("workspace","42").header("Host","attacker.invalid")).andExpect(status().isFound()).andReturn();
        String location=result.getResponse().getHeader("Location");
        assertThat(location).startsWith("https://orcid.org/oauth/authorize?").contains("scope=/authenticate","frelog.p-e.kr").doesNotContain("never-expose-test-secret","attacker.invalid");
        var query=org.springframework.web.util.UriComponentsBuilder.fromUriString(location).build().getQueryParams();
        var cookie=result.getResponse().getCookie("__Secure-IRADE_ORCID_FLOW");
        assertThat(cookie).isNotNull();assertThat(cookie.isHttpOnly()).isTrue();assertThat(cookie.getSecure()).isTrue();assertThat(cookie.getPath()).isEqualTo("/api/irade/auth");
        var flow=new Flow(query.getFirst("state"),cookie);
        var attempt=attempts.findById(OrcidService.hash(flow.state)).orElseThrow();
        assertThat(attempt.browserHash).isNotEqualTo(cookie.getValue());assertThat(attempt.id).isNotEqualTo(flow.state);return flow;
    }
    Cookie callback(Flow flow) throws Exception {
        var result=mvc.perform(get("/api/irade/auth/orcid/callback").secure(true).cookie(flow.browser).param("state",flow.state).param("code","valid-code").header("Host","attacker.invalid"))
                .andExpect(status().isSeeOther()).andExpect(redirectedUrl(ORIGIN+"/irade/login?orcid=success&workspace=42")).andReturn();
        assertThat(attempts.findById(OrcidService.hash(flow.state)).orElseThrow().consumed).isTrue();
        return Arrays.stream(result.getResponse().getCookies()).filter(c->c.getName().equals("__Secure-IRADE_ORCID_LOGIN")&&c.getMaxAge()>0).findFirst().orElseThrow();
    }
    String login() throws Exception {
        var cookie=callback(begin());
        var result=mvc.perform(post("/api/irade/auth/session").secure(true).cookie(cookie).header("Origin",ORIGIN)).andExpect(status().isOk()).andExpect(header().string("Cache-Control","no-store")).andExpect(jsonPath("$.orcid").value(ID)).andReturn();
        return json.readTree(result.getResponse().getContentAsString()).path("login").path("accessToken").asString();
    }
    @Test void registersEmailPrivateResearcherAndAuthenticatesWorkspaceSearch() throws Exception {
        String token=login();var account=users.findAll().getFirst();
        assertThat(account.getEmail()).isNull();assertThat(account.getPasswordHash()).isNull();assertThat(account.getRole()).isEqualTo(Role.ROLE_USER);assertThat(account.getOrcid()).isEqualTo(ID);
        assertThat(tokens.isOrcidToken(token)).isTrue();assertThat(tokens.getUserIdFromToken(token)).isEqualTo(account.getUserId());
        mvc.perform(get("/api/irade/auth/me").header("Authorization","Bearer "+token)).andExpect(status().isOk()).andExpect(jsonPath("$.orcid").value(ID));
        when(canvases.searchCanvases(eq("proof"),any())).thenReturn(List.of(new CanvasSummaryResponse(42,null,"notes","Proof workspace",1,"private")));
        mvc.perform(get("/api/irade/workspaces").param("q","proof").header("Authorization","Bearer "+token)).andExpect(status().isOk()).andExpect(jsonPath("$[0].workspaceId").value(42)).andExpect(jsonPath("$[0].name").value("Proof workspace"));
        verify(canvases).searchCanvases(eq("proof"),argThat(user->user.getUserId().equals(account.getUserId())));
        mvc.perform(post("/api/irade/workspaces/42/access").header("Authorization","Bearer "+token).contentType("application/json").content("{}" )).andExpect(status().isOk());
        verify(canvases).accessCanvas(eq(42),any(),argThat(user->user.getUserId().equals(account.getUserId())),isNull(),isNull());
    }
    @Test void repeatedLoginReusesAccountAndHandoffCannotBeConsumedTwice() throws Exception {
        var cookie=callback(begin());
        mvc.perform(post("/api/irade/auth/session").cookie(cookie).header("Origin","https://attacker.invalid")).andExpect(status().isForbidden());
        mvc.perform(post("/api/irade/auth/session").cookie(cookie).header("Origin",ORIGIN)).andExpect(status().isOk());
        mvc.perform(post("/api/irade/auth/session").cookie(cookie).header("Origin",ORIGIN)).andExpect(status().isUnauthorized());
        Long id=users.findAll().getFirst().getUserId();login();assertThat(users.findAll()).hasSize(1);assertThat(users.findAll().getFirst().getUserId()).isEqualTo(id);
    }
    @Test void wrongBrowserStateExpiryAndCallbackReplayNeverReachProvider() throws Exception {
        var flow=begin();
        mvc.perform(get("/api/irade/auth/orcid/callback").cookie(new Cookie(flow.browser.getName(),"A".repeat(43))).param("state",flow.state).param("code","valid-code")).andExpect(redirectedUrl(ORIGIN+"/irade/login?orcid=failed"));
        verifyNoInteractions(provider);callback(flow);clearInvocations(provider);
        mvc.perform(get("/api/irade/auth/orcid/callback").cookie(flow.browser).param("state",flow.state).param("code","valid-code")).andExpect(redirectedUrl(ORIGIN+"/irade/login?orcid=failed"));verifyNoInteractions(provider);
        var expired=begin();var row=attempts.findById(OrcidService.hash(expired.state)).orElseThrow();row.expiresAt=LocalDateTime.now(java.time.ZoneOffset.UTC).minusMinutes(1);attempts.save(row);
        mvc.perform(get("/api/irade/auth/orcid/callback").cookie(expired.browser).param("state",expired.state).param("code","valid-code")).andExpect(redirectedUrl(ORIGIN+"/irade/login?orcid=failed"));verifyNoInteractions(provider);
    }
    @Test void suspendedAccountAndRemovedIdentityRejectOldTokens() throws Exception {
        var token=login();var user=users.findAll().getFirst();user.setStatus(UserStatus.SUSPENDED);users.saveAndFlush(user);
        mvc.perform(get("/api/irade/auth/me").header("Authorization","Bearer "+token)).andExpect(status().isUnauthorized());
        var flow=begin();mvc.perform(get("/api/irade/auth/orcid/callback").cookie(flow.browser).param("state",flow.state).param("code","valid-code")).andExpect(redirectedUrl(ORIGIN+"/irade/login?orcid=failed&workspace=42"));
        user.setStatus(UserStatus.ACTIVE);user.setOrcid(null);user.setOrcidIssuer(null);users.saveAndFlush(user);
        mvc.perform(get("/api/irade/auth/me").header("Authorization","Bearer "+token)).andExpect(status().isUnauthorized());
    }
    @Test void existingAccountMustExplicitlyAuthenticateBeforeLinking() throws Exception {
        var user=users.saveAndFlush(new User("existing@example.invalid",null,"Existing researcher",1,Role.ROLE_USER));
        String token=tokens.createToken(user.getEmail(),1,user.getNickname(),"ROLE_USER","127.0.0.1");
        mvc.perform(post("/api/irade/auth/orcid/link").contentType("application/json").content("{}").header("Origin",ORIGIN)).andExpect(status().isUnauthorized());
        mvc.perform(post("/api/irade/auth/orcid/link").contentType("application/json").content("{}").header("Authorization","Bearer "+token).header("Origin","https://attacker.invalid")).andExpect(status().isForbidden());
        var start=mvc.perform(post("/api/irade/auth/orcid/link").contentType("application/json").content("{\"workspace\":42}").header("Authorization","Bearer "+token).header("Origin",ORIGIN)).andExpect(status().isOk()).andReturn();
        String url=json.readTree(start.getResponse().getContentAsString()).path("authorizationUrl").asString();String state=org.springframework.web.util.UriComponentsBuilder.fromUriString(url).build().getQueryParams().getFirst("state");
        callback(new Flow(state,start.getResponse().getCookie("__Secure-IRADE_ORCID_FLOW")));
        assertThat(users.findById(user.getUserId()).orElseThrow().getOrcid()).isEqualTo(ID);assertThat(users.findAll()).hasSize(1);
    }
    @Test void authenticatedOrcidIsRequiredAndCreationIsPrivate() throws Exception {
        mvc.perform(get("/api/irade/workspaces")).andExpect(status().isUnauthorized());
        var user=users.saveAndFlush(new User("unlinked@example.invalid",null,"Unlinked",1,Role.ROLE_USER));String ordinary=tokens.createToken(user.getEmail(),1,user.getNickname(),"ROLE_USER","127.0.0.1");
        mvc.perform(get("/api/irade/workspaces").header("Authorization","Bearer "+ordinary)).andExpect(status().isForbidden());verifyNoInteractions(canvases);
        String token=login();when(canvases.createCanvas(eq("Research"),isNull(),isNull(),isNull(),any(),eq("private"))).thenReturn(new CanvasSummaryResponse(43,null,null,"Research",1,"private"));
        mvc.perform(post("/api/irade/workspaces").header("Authorization","Bearer "+token).contentType("application/json").content("{\"name\":\"Research\"}")).andExpect(status().isCreated()).andExpect(jsonPath("$.workspaceId").value(43));
    }
    @Test void withdrawnResearcherLosesIdentityAndRejoinsAsANewAccount() throws Exception {
        String token=login();var old=users.findAll().getFirst();Long id=old.getUserId();
        mvc.perform(post("/api/auth/me").contentType("application/json").content("{\"token\":\""+token+"\"}")).andExpect(status().isOk()).andExpect(jsonPath("$.user_id").value(id));
        auth.deleteUser(old.getNickname(),old.getTagNumber(),new CustomUserDetails(old));
        mvc.perform(post("/api/auth/me").contentType("application/json").content("{\"token\":\""+token+"\"}")).andExpect(status().isUnauthorized());
        var tombstone=users.findById(id).orElseThrow();assertThat(tombstone.getOrcid()).isNull();assertThat(tombstone.getOrcidIssuer()).isNull();assertThat(tombstone.getStatus()).isEqualTo(UserStatus.WITHDRAWN);
        mvc.perform(get("/api/irade/auth/me").header("Authorization","Bearer "+token)).andExpect(status().isUnauthorized());
        String fresh=login();assertThat(tokens.getUserIdFromToken(fresh)).isNotEqualTo(id);
    }

    @Test void agoraReturnIsBoundToTheOriginalStateAndUsesTheSameIdentity() throws Exception {
        mvc.perform(get("/api/auth/providers")).andExpect(jsonPath("$.orcid").value(true));
        mvc.perform(get("/api/irade/auth/orcid/start").param("app","https://attacker.invalid")).andExpect(status().isBadRequest());
        var start=mvc.perform(get("/api/irade/auth/orcid/start").param("app","agora").secure(true)).andExpect(status().isFound()).andReturn();
        String state=org.springframework.web.util.UriComponentsBuilder.fromUriString(start.getResponse().getHeader("Location")).build().getQueryParams().getFirst("state");
        assertThat(state).startsWith("agora.");
        var browser=start.getResponse().getCookie("__Secure-IRADE_ORCID_FLOW");
        mvc.perform(get("/api/irade/auth/orcid/callback").cookie(browser).param("state",state.substring(6)).param("code","valid-code")).andExpect(redirectedUrl(ORIGIN+"/irade/login?orcid=failed"));
        verifyNoInteractions(provider);
        var result=mvc.perform(get("/api/irade/auth/orcid/callback").cookie(browser).param("state",state).param("code","valid-code")).andExpect(redirectedUrl(ORIGIN+"/login?orcid=success")).andReturn();
        Cookie handoff=Arrays.stream(result.getResponse().getCookies()).filter(c->c.getName().equals("__Secure-IRADE_ORCID_LOGIN")&&c.getMaxAge()>0).findFirst().orElseThrow();
        var session=mvc.perform(post("/api/irade/auth/session").cookie(handoff).header("Origin",ORIGIN)).andExpect(status().isOk()).andReturn();
        String token=json.readTree(session.getResponse().getContentAsString()).path("login").path("accessToken").asString();
        mvc.perform(get("/api/auth/me").header("Authorization","Bearer "+token)).andExpect(status().isOk()).andExpect(jsonPath("$.email").isEmpty());
        Long id=tokens.getUserIdFromToken(token);assertThat(tokens.getUserIdFromToken(login())).isEqualTo(id);
    }
    @Test void agoraCancellationAndExpiredStateReturnToAgoraWithoutIssuingTokens() throws Exception {
        var start=mvc.perform(get("/api/irade/auth/orcid/start").param("app","agora")).andReturn();
        String state=org.springframework.web.util.UriComponentsBuilder.fromUriString(start.getResponse().getHeader("Location")).build().getQueryParams().getFirst("state");
        var browser=start.getResponse().getCookie("__Secure-IRADE_ORCID_FLOW");
        mvc.perform(get("/api/irade/auth/orcid/callback").cookie(browser).param("state",state).param("error","access_denied")).andExpect(redirectedUrl(ORIGIN+"/login?orcid=cancelled"));
        mvc.perform(get("/api/irade/auth/orcid/callback").cookie(browser).param("state",state).param("code","valid-code")).andExpect(redirectedUrl(ORIGIN+"/login?orcid=failed"));
        verifyNoInteractions(provider);assertThat(users.findAll()).isEmpty();
    }

}
