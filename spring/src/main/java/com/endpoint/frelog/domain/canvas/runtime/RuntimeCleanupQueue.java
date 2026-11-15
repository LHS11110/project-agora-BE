package com.endpoint.frelog.domain.canvas.runtime;

import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import java.util.List;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Repository;

/** No foreign key: this durable task must outlive its deleted canvas or workspace. */
@Repository
public class RuntimeCleanupQueue {
    private final JdbcTemplate jdbc;
    public RuntimeCleanupQueue(JdbcTemplate jdbc) { this.jdbc = jdbc; }

    private static String tableFor(DocumentScope scope) {
        return scope == DocumentScope.WORKSPACE ? "dbo.workspace_runtime_cleanup" : "dbo.canvas_runtime_cleanup";
    }

    private static String idColFor(DocumentScope scope) {
        return scope == DocumentScope.WORKSPACE ? "workspace_id" : "canvas_id";
    }

    @jakarta.annotation.PostConstruct
    public void verifySchema() {
        jdbc.queryForList("SELECT TOP (0) canvas_id,attempt_count,next_attempt_at FROM dbo.canvas_runtime_cleanup");
        jdbc.queryForList("SELECT TOP (0) workspace_id,attempt_count,next_attempt_at FROM dbo.workspace_runtime_cleanup");
    }

    public void enqueue(Integer id) {
        enqueue(DocumentScope.CANVAS, id);
    }

    public void enqueue(DocumentScope scope, Integer id) {
        var now = LocalDateTime.now(ZoneOffset.UTC);
        jdbc.update(
                "INSERT INTO " + tableFor(scope) + "(" + idColFor(scope) + ",created_at,next_attempt_at) VALUES (?,?,?)",
                id, now, now);
    }

    public List<Integer> due() {
        return due(DocumentScope.CANVAS);
    }

    public List<Integer> due(DocumentScope scope) {
        String col = idColFor(scope);
        return jdbc.queryForList(
                "SELECT TOP (16) " + col + " FROM " + tableFor(scope) + " WHERE next_attempt_at <= ? ORDER BY next_attempt_at, " + col,
                Integer.class, LocalDateTime.now(ZoneOffset.UTC));
    }

    public boolean contains(Integer id) {
        return contains(DocumentScope.CANVAS, id);
    }

    public boolean contains(DocumentScope scope, Integer id) {
        Integer count = jdbc.queryForObject(
                "SELECT COUNT(*) FROM " + tableFor(scope) + " WHERE " + idColFor(scope) + "=?",
                Integer.class, id);
        return count != null && count > 0;
    }

    public void complete(Integer id) {
        complete(DocumentScope.CANVAS, id);
    }

    public void complete(DocumentScope scope, Integer id) {
        jdbc.update("DELETE FROM " + tableFor(scope) + " WHERE " + idColFor(scope) + "=?", id);
    }

    public void retry(Integer id) {
        retry(DocumentScope.CANVAS, id);
    }

    public void retry(DocumentScope scope, Integer id) {
        jdbc.update(
                "UPDATE " + tableFor(scope) + " SET attempt_count=attempt_count+1,next_attempt_at=? WHERE " + idColFor(scope) + "=?",
                LocalDateTime.now(ZoneOffset.UTC).plusSeconds(30), id);
    }
}
