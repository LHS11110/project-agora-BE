package com.endpoint.frelog.domain.canvas.runtime;

import java.util.List;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Repository;

/** No foreign key: this durable task must outlive its deleted canvas. */
@Repository
public class RuntimeCleanupQueue {
    private final JdbcTemplate jdbc;
    public RuntimeCleanupQueue(JdbcTemplate jdbc) { this.jdbc = jdbc; }
    @jakarta.annotation.PostConstruct
    public void verifySchema() {
        jdbc.queryForList("SELECT TOP (0) canvas_id,attempt_count,next_attempt_at FROM dbo.canvas_runtime_cleanup");
    }
    public void enqueue(Integer id) {
        var now=LocalDateTime.now(ZoneOffset.UTC);
        jdbc.update("INSERT INTO dbo.canvas_runtime_cleanup(canvas_id,created_at,next_attempt_at) VALUES (?,?,?)", id,now,now);
    }
    public List<Integer> due() {
        return jdbc.queryForList("SELECT TOP (16) canvas_id FROM dbo.canvas_runtime_cleanup WHERE next_attempt_at <= ? ORDER BY next_attempt_at, canvas_id", Integer.class,LocalDateTime.now(ZoneOffset.UTC));
    }
    public boolean contains(Integer id) {
        return jdbc.queryForObject("SELECT COUNT(*) FROM dbo.canvas_runtime_cleanup WHERE canvas_id=?", Integer.class, id) > 0;
    }
    public void complete(Integer id) { jdbc.update("DELETE FROM dbo.canvas_runtime_cleanup WHERE canvas_id=?", id); }
    public void retry(Integer id) {
        jdbc.update("UPDATE dbo.canvas_runtime_cleanup SET attempt_count=attempt_count+1,next_attempt_at=? WHERE canvas_id=?", LocalDateTime.now(ZoneOffset.UTC).plusSeconds(30),id);
    }
}
