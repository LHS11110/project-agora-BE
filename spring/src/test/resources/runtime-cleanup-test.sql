CREATE SCHEMA IF NOT EXISTS dbo;
CREATE TABLE IF NOT EXISTS dbo.canvas_runtime_cleanup (
 canvas_id INT PRIMARY KEY,
 created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 attempt_count INT NOT NULL DEFAULT 0,
 next_attempt_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS dbo.workspace_runtime_cleanup (
 workspace_id INT PRIMARY KEY,
 created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 attempt_count INT NOT NULL DEFAULT 0,
 next_attempt_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);
-- Administrator-managed activity table (dbo.user_canvas_activity in init-mssql.sql).
-- The users FK is omitted because Hibernate creates dbo.users after this script.
CREATE TABLE IF NOT EXISTS dbo.user_canvas_activity (
 user_id INT NOT NULL,
 canvas_id INT NOT NULL,
 client_key VARCHAR(160) NOT NULL,
 activity_date DATE NOT NULL,
 created_count BIGINT NOT NULL DEFAULT 0,
 updated_count BIGINT NOT NULL DEFAULT 0,
 deleted_count BIGINT NOT NULL DEFAULT 0,
 updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 PRIMARY KEY (user_id, activity_date, canvas_id, client_key)
);
