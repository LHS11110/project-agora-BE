package com.endpoint.frelog.domain.user.controller;

import com.endpoint.frelog.domain.canvas.bot.CanvasBotService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import java.time.Instant;
import org.springframework.http.HttpStatus;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;

@RestController
@RequestMapping("/api/users/agent-terms")
public class AgentTermsController {
    private static final String VERSION = "agent-local-v2";
    private final JdbcTemplate jdbc;

    public AgentTermsController(JdbcTemplate jdbc) {
        this.jdbc = jdbc;
    }

    public record Status(String version, String body, boolean accepted, Instant decidedAt) {}
    public record Decision(String version, Boolean accepted) {}

    @GetMapping
    public Status status(@AuthenticationPrincipal CustomUserDetails user) {
        return load(account(user));
    }

    @PutMapping
    @Transactional
    public Status decide(@AuthenticationPrincipal CustomUserDetails user, @RequestBody Decision decision) {
        long id = account(user);
        if (!VERSION.equals(decision.version()) || decision.accepted() == null) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST);
        }
        jdbc.update("""
            MERGE dbo.agent_term_decisions WITH (HOLDLOCK) AS target
            USING (SELECT user_id, CAST(? AS VARCHAR(40)) AS terms_version, CAST(? AS BIT) AS accepted
                   FROM dbo.users WITH (UPDLOCK, HOLDLOCK) WHERE user_id=? AND status='ACTIVE') AS source
            ON target.user_id=source.user_id AND target.terms_version=source.terms_version
            WHEN MATCHED AND target.accepted<>source.accepted THEN
                UPDATE SET accepted=source.accepted, decision_at=SYSUTCDATETIME()
            WHEN NOT MATCHED THEN
                INSERT (user_id,terms_version,accepted,decision_at)
                VALUES (source.user_id,source.terms_version,source.accepted,SYSUTCDATETIME());
            """, VERSION, decision.accepted(), id);
        return load(id);
    }

    private Status load(long id) {
        return jdbc.queryForObject("""
            SELECT t.version, t.body, d.accepted, d.decision_at
            FROM dbo.agent_terms t LEFT JOIN dbo.agent_term_decisions d
                ON d.terms_version=t.version AND d.user_id=?
            WHERE t.version=?
            """, (row, index) -> {
                var decided = row.getTimestamp("decision_at", java.util.Calendar.getInstance(java.util.TimeZone.getTimeZone("UTC")));
                return new Status(row.getString("version"), row.getString("body"),
                    row.getBoolean("accepted"), decided == null ? null : decided.toInstant());
            }, id, VERSION);
    }

    private long account(CustomUserDetails user) {
        if (user == null || !user.isEnabled() || CanvasBotService.isBot(user.getUser())) {
            throw new ResponseStatusException(HttpStatus.FORBIDDEN);
        }
        return user.getUserId();
    }
}
