#include "service_memory/RegistryServiceMemory.hpp"
#include "memory/MssqlMemory.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include <charconv>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <utility>
namespace {
std::optional<std::string> cell(const SqlRow& row, std::size_t index) {
    return index < row.values.size() ? row.values[index] : std::nullopt;
}
template <typename Integer> std::optional<Integer> number(const std::optional<std::string>& text) {
    if (!text || text->empty()) return std::nullopt;
    Integer value{}; const auto* end = text->data() + text->size();
    const auto result = std::from_chars(text->data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;
    return value;
}
const SqlRow* lastRow(const SqlQueryResult& result) {
    for (auto set = result.result_sets.rbegin(); set != result.result_sets.rend(); ++set)
        if (!set->rows.empty()) return &set->rows.back();
    return nullptr;
}
std::optional<int> scalar(const std::optional<SqlQueryResult>& result) {
    if (!result) return std::nullopt;
    const auto* row = lastRow(*result);
    return row ? number<int>(cell(*row, 0)) : std::nullopt;
}
}
RegistryServiceMemory::RegistryServiceMemory(const std::string& host, int port,
        const std::string& user, const std::string& password, const std::string& database)
    : RegistryServiceMemory(std::make_unique<MssqlMemory>(host, port, user, password, database)) {}
RegistryServiceMemory::RegistryServiceMemory(std::unique_ptr<SqlExecutor> storage) : storage_(std::move(storage)) {
    if (!storage_) throw std::invalid_argument("RegistryServiceMemory storage must not be null");
}
RegistryServiceMemory::~RegistryServiceMemory() = default;

bool RegistryServiceMemory::registerServer(const std::string& ip, int rest_port, int ws_port) {
    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "IF EXISTS (SELECT 1 FROM cpp_server WITH (UPDLOCK, HOLDLOCK) WHERE server_ip = @ip AND server_port = @rest_port) "
        "BEGIN "
        "   UPDATE cpp_server SET ws_port = @ws_port, is_activated = 1, last_heartbeat_at = SYSUTCDATETIME() WHERE server_ip = @ip AND server_port = @rest_port; "
        "END "
        "ELSE "
        "BEGIN "
        "   INSERT INTO cpp_server (server_ip, server_port, ws_port, is_activated, created_at, last_heartbeat_at) VALUES (@ip, @rest_port, @ws_port, 1, SYSUTCDATETIME(), SYSUTCDATETIME()); "
        "END; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addVarchar("@ip", ip).addVarchar("@rest_port", std::to_string(rest_port))
       .addVarchar("@ws_port", std::to_string(ws_port));
    return storage_->execute(sql);
}

bool RegistryServiceMemory::heartbeatServer(const std::string& ip, int rest_port) {
    SqlCommand sql(
        "UPDATE cpp_server SET last_heartbeat_at = SYSUTCDATETIME(), is_activated = 1 "
        "WHERE server_ip = @ip AND server_port = @rest_port; "
        "SELECT @@ROWCOUNT AS affected;");
    sql.addVarchar("@ip", ip).addVarchar("@rest_port", std::to_string(rest_port));
    return scalar(storage_->query(sql)) == 1;
}

bool RegistryServiceMemory::unregisterServer(const std::string& ip, int rest_port) {
    return setServerInactive(ip, rest_port);
}

bool RegistryServiceMemory::setServerInactive(const std::string& ip, int rest_port) {
    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "UPDATE cpp_server SET is_activated = 0, last_heartbeat_at = SYSUTCDATETIME() WHERE server_ip = @ip AND server_port = @rest_port; "
        "COMMIT TRAN; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addVarchar("@ip", ip).addVarchar("@rest_port", std::to_string(rest_port));
    return storage_->execute(sql);
}

CanvasRedisAllocation RegistryServiceMemory::getOrAllocateRedisAndSetCached(int canvasId, const std::string& cppServerIp, int cppServerPort) {
    return getOrAllocateRedisAndSetCached(SpaceScope::Canvas, canvasId, cppServerIp, cppServerPort);
}

CanvasRedisAllocation RegistryServiceMemory::getOrAllocateRedisAndSetCached(SpaceScope scope, int canvasId, const std::string& cppServerIp, int cppServerPort) {
    const std::string table = scope == SpaceScope::Workspace ? "workspace_info" : "canvas_info";
    const std::string id_col = scope == SpaceScope::Workspace ? "workspace_id" : "canvas_id";
    const std::string scope_wire = std::string(spaceScopeWire(scope));
    const auto failedAllocation = [] {
        CanvasRedisAllocation allocation;
        allocation.redis_ip = "ERROR";
        return allocation;
    };
    const auto recordFailure = [canvasId, &scope_wire](const std::string& message, const std::string& errorCode) {
        ElasticsearchBulkLogBuffer::instance().record(
            "redis-load-balancer", "canvas_redis_allocation", "ERROR", message,
            {{"scope", scope_wire}, {"canvas_id", canvasId}}, "failure", errorCode);
        std::cerr << "[MssqlClient][RedisLB] " << scope_wire << " #" << canvasId << " allocation failed: "
                  << message << " (" << errorCode << ")\n";
    };

    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "  DECLARE @is_cached BIT = NULL, @was_cached BIT = 0, @redis_ip NVARCHAR(50), @redis_port NVARCHAR(10), @assigned_cpp_id INT; "
        "  DECLARE @allocation_strategy NVARCHAR(16) = N'NONE', @candidate_count INT = 0, @selected_canvas_count BIGINT = NULL; "
        "  DECLARE @candidate1_ip NVARCHAR(50), @candidate1_port NVARCHAR(10), @candidate1_count BIGINT; "
        "  DECLARE @candidate2_ip NVARCHAR(50), @candidate2_port NVARCHAR(10), @candidate2_count BIGINT; "
        "  SELECT @is_cached = is_cached, @was_cached = is_cached, @redis_ip = r.redis_ip, @redis_port = CONVERT(NVARCHAR(10), r.redis_port), @assigned_cpp_id = c.cpp_server_id "
        "    FROM " + table + " c WITH (UPDLOCK, ROWLOCK) "
        "    LEFT JOIN redis_server r ON c.redis_id = r.redis_id AND r.is_activated = 1 "
        "    WHERE c." + id_col + " = @canvas_id; "
        "  DECLARE @my_cpp_id INT; "
        "  SELECT @my_cpp_id = server_id FROM cpp_server WHERE server_ip = @cpp_ip AND server_port = @cpp_port AND is_activated = 1; "
        "  IF @my_cpp_id IS NULL THROW 50001, 'C++ server is not registered or active', 1; "
        "  IF @is_cached IS NULL "
        "  BEGIN SET @redis_ip = N'NOT_FOUND'; SET @redis_port = N'0'; END "
        "  ELSE IF @is_cached = 1 AND @assigned_cpp_id IS NOT NULL AND @assigned_cpp_id != @my_cpp_id "
        "  BEGIN SET @redis_ip = N'WRONG_SERVER'; SET @redis_port = N'0'; END "
        "  ELSE IF @is_cached = 1 AND @redis_ip IS NULL "
        "  BEGIN SET @redis_ip = N'ERROR'; SET @redis_port = N'0'; END "
        "  ELSE "
        "  BEGIN "
        "    IF @redis_ip IS NULL "
        "    BEGIN "
        "      DECLARE @new_redis_id INT; "
        "      DECLARE @redis_candidates TABLE (candidate_order INT IDENTITY(1,1) NOT NULL PRIMARY KEY, redis_id INT NOT NULL, redis_ip NVARCHAR(50) NOT NULL, redis_port NVARCHAR(10) NOT NULL, cached_canvas_count BIGINT NOT NULL); "
        "      INSERT INTO @redis_candidates (redis_id, redis_ip, redis_port, cached_canvas_count) "
        "      SELECT candidate.redis_id, candidate.redis_ip, CONVERT(NVARCHAR(10), candidate.redis_port), "
        // The load count is a balancing estimate across both canvases and workspaces, so avoid shared locks.
        "             ((SELECT COUNT_BIG(*) FROM canvas_info ci WITH (NOLOCK) WHERE ci.redis_id = candidate.redis_id AND ci.is_cached = 1) + "
        "              (SELECT COUNT_BIG(*) FROM workspace_info wi WITH (NOLOCK) WHERE wi.redis_id = candidate.redis_id AND wi.is_cached = 1)) "
        "      FROM (SELECT TOP (2) redis_id, redis_ip, redis_port FROM redis_server WHERE is_activated = 1 ORDER BY NEWID()) AS candidate; "
        "      SELECT @candidate_count = COUNT(*) FROM @redis_candidates; "
        "      IF @candidate_count = 0 THROW 50002, 'No active Redis server is available', 1; "
        "      SELECT TOP (1) @new_redis_id = redis_id, @redis_ip = redis_ip, @redis_port = redis_port, @selected_canvas_count = cached_canvas_count "
        "      FROM @redis_candidates ORDER BY cached_canvas_count ASC, NEWID(); "
        "      SELECT @candidate1_ip = MAX(CASE WHEN candidate_order = 1 THEN redis_ip END), "
        "             @candidate1_port = MAX(CASE WHEN candidate_order = 1 THEN redis_port END), "
        "             @candidate1_count = MAX(CASE WHEN candidate_order = 1 THEN cached_canvas_count END), "
        "             @candidate2_ip = MAX(CASE WHEN candidate_order = 2 THEN redis_ip END), "
        "             @candidate2_port = MAX(CASE WHEN candidate_order = 2 THEN redis_port END), "
        "             @candidate2_count = MAX(CASE WHEN candidate_order = 2 THEN cached_canvas_count END) "
        "      FROM @redis_candidates; "
        "      SET @allocation_strategy = CASE WHEN @candidate_count = 1 THEN N'SINGLE' ELSE N'P2C' END; "
        "      UPDATE " + table + " SET redis_id = @new_redis_id WHERE " + id_col + " = @canvas_id; "
        "    END "
        "    ELSE SET @allocation_strategy = N'EXISTING'; "
        "    UPDATE " + table + " SET is_cached = 1, cpp_server_id = @my_cpp_id, updated_at = SYSUTCDATETIME() WHERE " + id_col + " = @canvas_id; "
        "  END "
        "  COMMIT TRAN; "
        "  SELECT COALESCE(@redis_ip, N'') AS redis_ip, COALESCE(@redis_port, N'') AS redis_port, "
        "         CONVERT(VARCHAR(5), @was_cached) AS was_cached, @allocation_strategy AS allocation_strategy, "
        "         CONVERT(VARCHAR(10), @candidate_count) AS candidate_count, "
        "         COALESCE(@candidate1_ip, N'') AS candidate1_ip, COALESCE(@candidate1_port, N'') AS candidate1_port, "
        "         COALESCE(CONVERT(VARCHAR(20), @candidate1_count), '') AS candidate1_count, "
        "         COALESCE(@candidate2_ip, N'') AS candidate2_ip, COALESCE(@candidate2_port, N'') AS candidate2_port, "
        "         COALESCE(CONVERT(VARCHAR(20), @candidate2_count), '') AS candidate2_count, "
        "         COALESCE(CONVERT(VARCHAR(20), @selected_canvas_count), '') AS selected_canvas_count; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addInt("@canvas_id", canvasId).addVarchar("@cpp_ip", cppServerIp)
       .addVarchar("@cpp_port", std::to_string(cppServerPort));
    const auto result = storage_->query(sql);
    const auto* row = result ? lastRow(*result) : nullptr;
    if (!row || row->values.size() != 12) {
        recordFailure("Could not execute or read the Redis allocation transaction", "DB_ALLOCATION_RESULT_FAILED");
        return failedAllocation();
    }
    const std::string found_ip = cell(*row, 0).value_or("");
    const int found_port = number<int>(cell(*row, 1)).value_or(0);
    const bool was_cached = cell(*row, 2) == "1";
    const std::string allocation_strategy = cell(*row, 3).value_or("");
    const auto candidate_count = number<int>(cell(*row, 4));
    if (!candidate_count || *candidate_count < 0 || *candidate_count > 2) return failedAllocation();
    std::int64_t selected_cached_canvas_count = -1;
    if (const auto text = cell(*row, 11); text && !text->empty()) {
        const auto count = number<std::int64_t>(text);
        if (!count) return failedAllocation();
        selected_cached_canvas_count = *count;
    }
    std::vector<RedisAllocationCandidate> candidates;
    for (int index = 0; index < *candidate_count; ++index) {
        const auto offset = static_cast<std::size_t>(5 + index * 3);
        const auto ip = cell(*row, offset); const auto port = number<int>(cell(*row, offset + 1));
        const auto count = number<std::int64_t>(cell(*row, offset + 2));
        if (!ip || ip->empty() || !port || !count) return failedAllocation();
        candidates.push_back({*ip, *port, *count});
    }
    if (found_ip == "NOT_FOUND" || found_ip == "WRONG_SERVER" || found_ip == "ERROR") {
        const bool is_error = found_ip == "ERROR";
        const std::string message = found_ip == "NOT_FOUND"
                ? "Canvas does not exist in the allocation table"
                : found_ip == "WRONG_SERVER"
                    ? "Canvas is already allocated to another C++ server"
                    : "Canvas is marked cached but has no active Redis assignment";
        ElasticsearchBulkLogBuffer::instance().record(
            "redis-load-balancer", "canvas_redis_allocation", is_error ? "ERROR" : "WARN", message,
            {{"canvas_id", canvasId}, {"allocation_strategy", allocation_strategy}},
            is_error ? "failure" : "rejected", found_ip);
        std::cerr << "[MssqlClient][RedisLB] Canvas #" << canvasId << " allocation "
                  << (is_error ? "failed" : "rejected") << ": " << message << "\n";
        auto allocation = failedAllocation();
        allocation.redis_ip = found_ip;
        allocation.was_cached = was_cached;
        allocation.allocation_strategy = allocation_strategy;
        return allocation;
    }

    if (found_ip.empty() || found_port <= 0) {
        recordFailure("Allocation returned no usable Redis endpoint", "DB_INVALID_REDIS_ENDPOINT");
        auto allocation = failedAllocation();
        allocation.was_cached = was_cached;
        return allocation;
    }

    nlohmann::json candidate_details = nlohmann::json::array();
    for (const auto& candidate : candidates) {
        candidate_details.push_back({
            {"redis_ip", candidate.redis_ip},
            {"redis_port", candidate.redis_port},
            {"cached_canvas_count", candidate.cached_canvas_count}
        });
    }
    const nlohmann::json selected_load = selected_cached_canvas_count >= 0
            ? nlohmann::json(selected_cached_canvas_count)
            : nlohmann::json(nullptr);
    const nlohmann::json allocation_details = {
        {"canvas_id", canvasId},
        {"strategy", allocation_strategy},
        {"candidates", candidate_details},
        {"selected_redis_ip", found_ip},
        {"selected_redis_port", found_port},
        {"selected_cached_canvas_count", selected_load},
        {"was_cached", was_cached}
    };
    ElasticsearchBulkLogBuffer::instance().record(
        "redis-load-balancer", "canvas_redis_allocation", "INFO",
        "Selected Redis service for canvas allocation", allocation_details, "success");

    std::cout << "[MssqlClient][RedisLB] Canvas #" << canvasId << " strategy="
              << allocation_strategy << " candidates=";
    if (candidates.empty()) {
        std::cout << "existing-assignment";
    } else {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << candidates[i].redis_ip << ':' << candidates[i].redis_port
                      << "(cached_canvases=" << candidates[i].cached_canvas_count << ')';
        }
    }
    std::cout << " selected=" << found_ip << ':' << found_port;
    if (selected_cached_canvas_count >= 0) {
        std::cout << "(cached_canvases=" << selected_cached_canvas_count << ')';
    }
    std::cout << "\n";

    CanvasRedisAllocation allocation;
    allocation.redis_ip = found_ip;
    allocation.redis_port = found_port;
    allocation.was_cached = was_cached;
    allocation.allocation_strategy = allocation_strategy;
    allocation.candidates = std::move(candidates);
    allocation.selected_cached_canvas_count = selected_cached_canvas_count;
    return allocation;
}

bool RegistryServiceMemory::updateCanvasUncached(int canvasId, const std::string& cppServerIp, int cppServerPort) {
    return updateCanvasUncached(SpaceScope::Canvas, canvasId, cppServerIp, cppServerPort);
}

bool RegistryServiceMemory::updateCanvasUncached(SpaceScope scope, int canvasId, const std::string& cppServerIp, int cppServerPort) {
    const std::string table = scope == SpaceScope::Workspace ? "workspace_info" : "canvas_info";
    const std::string id_col = scope == SpaceScope::Workspace ? "workspace_id" : "canvas_id";
    SqlCommand sql(
        "BEGIN TRAN; "
        "BEGIN TRY "
        "DECLARE @server_id INT, @affected INT = 0, @is_cached BIT, @assigned_server_id INT; "
        "SELECT @server_id = server_id FROM cpp_server WHERE server_ip = @cpp_ip AND server_port = @cpp_port; "
        "SELECT @is_cached = is_cached, @assigned_server_id = cpp_server_id "
        "FROM " + table + " WITH (UPDLOCK, HOLDLOCK, ROWLOCK) WHERE " + id_col + " = @canvas_id; "
        "IF @is_cached = 1 AND @assigned_server_id = @server_id "
        "BEGIN "
        "    UPDATE " + table + " SET is_cached = 0, redis_id = NULL, cpp_server_id = NULL, updated_at = SYSUTCDATETIME() "
        "    WHERE " + id_col + " = @canvas_id AND is_cached = 1 AND cpp_server_id = @server_id; "
        "    SET @affected = @@ROWCOUNT; "
        "END "
        "COMMIT TRAN; "
        "SELECT @affected AS affected; "
        "END TRY "
        "BEGIN CATCH "
        "IF @@TRANCOUNT > 0 ROLLBACK TRAN; "
        "THROW; "
        "END CATCH;");
    sql.addInt("@canvas_id", canvasId).addVarchar("@cpp_ip", cppServerIp)
       .addVarchar("@cpp_port", std::to_string(cppServerPort));
    return scalar(storage_->query(sql)) == 1;
}

int RegistryServiceMemory::getActiveUserId(const std::string& nickname, int tagNumber) {
    return getActiveUserIdChecked(nickname, tagNumber).value_or(-1);
}

std::optional<int> RegistryServiceMemory::getActiveUserIdChecked(const std::string& nickname, int tagNumber) {
    if (nickname.empty() || tagNumber < 0) return -1;
    SqlCommand sql("SELECT user_id FROM users WHERE nickname = @nickname AND tag_number = @tag AND status = 'ACTIVE';");
    sql.addText("@nickname", nickname).addInt("@tag", tagNumber);
    const auto result = storage_->query(sql);
    if (!result) return std::nullopt;
    return scalar(result).value_or(-1);
}

std::optional<std::pair<std::string, int>> RegistryServiceMemory::getUserHandle(int userId) {
    if (userId <= 0) return std::nullopt;
    SqlCommand sql("SELECT nickname, tag_number FROM users WHERE user_id = @user_id;");
    sql.addInt("@user_id", userId);
    const auto result = storage_->query(sql);
    const auto* row = result ? lastRow(*result) : nullptr;
    if (!row) return std::nullopt;
    const auto nickname = cell(*row, 0); const auto tag = number<int>(cell(*row, 1));
    if (!nickname || !tag) return std::nullopt;
    return std::make_pair(*nickname, *tag);
}

bool RegistryServiceMemory::isCanvasAssignedToServer(int canvasId, const std::string& serverIp, int serverPort) {
    return isCanvasAssignedToServer(SpaceScope::Canvas, canvasId, serverIp, serverPort);
}

bool RegistryServiceMemory::isCanvasAssignedToServer(SpaceScope scope, int canvasId, const std::string& serverIp, int serverPort) {
    if (canvasId <= 0 || serverPort <= 0) return false;
    const std::string table = scope == SpaceScope::Workspace ? "workspace_info" : "canvas_info";
    const std::string id_col = scope == SpaceScope::Workspace ? "workspace_id" : "canvas_id";
    SqlCommand sql("SELECT COUNT(*) FROM " + table + " c JOIN cpp_server s ON s.server_id = c.cpp_server_id "
                   "WHERE c." + id_col + " = @canvas_id AND c.is_cached = 1 AND s.server_ip = @server_ip AND s.server_port = @server_port;");
    sql.addInt("@canvas_id", canvasId).addVarchar("@server_ip", serverIp)
       .addVarchar("@server_port", std::to_string(serverPort));
    return scalar(storage_->query(sql)) == 1;
}

std::optional<CanvasStorageAssignment> RegistryServiceMemory::getCanvasStorageAssignment(int canvasId) {
    return getCanvasStorageAssignment(SpaceScope::Canvas, canvasId);
}

std::optional<CanvasStorageAssignment> RegistryServiceMemory::getCanvasStorageAssignment(SpaceScope scope, int canvasId) {
    if (canvasId <= 0) return std::nullopt;
    const std::string table = scope == SpaceScope::Workspace ? "workspace_info" : "canvas_info";
    const std::string id_col = scope == SpaceScope::Workspace ? "workspace_id" : "canvas_id";
    SqlCommand sql(
        "SELECT CONVERT(VARCHAR(5), c.is_cached), "
        "COALESCE(s.server_ip, ''), COALESCE(CONVERT(VARCHAR(16), s.server_port), ''), "
        "COALESCE(r.redis_ip, ''), COALESCE(CONVERT(VARCHAR(16), r.redis_port), '') "
        "FROM " + table + " c "
        "LEFT JOIN cpp_server s ON s.server_id = c.cpp_server_id "
        "LEFT JOIN redis_server r ON r.redis_id = c.redis_id AND r.is_activated = 1 "
        "WHERE c." + id_col + " = @canvas_id;");
    sql.addInt("@canvas_id", canvasId);
    const auto result = storage_->query(sql);
    const auto* row = result ? lastRow(*result) : nullptr;
    if (!row || row->values.size() != 5) return std::nullopt;
    CanvasStorageAssignment assignment;
    assignment.is_cached = cell(*row, 0) == "1";
    assignment.cpp_server_ip = cell(*row, 1).value_or("");
    assignment.cpp_server_port = cell(*row, 2).value_or("");
    assignment.redis_ip = cell(*row, 3).value_or("");
    const auto port = cell(*row, 4);
    if (port && !port->empty()) {
        const auto parsed = number<int>(port); if (!parsed) return std::nullopt;
        assignment.redis_port = *parsed;
    }
    return assignment;
}


bool RegistryServiceMemory::storeCanvasActivity(int canvasId,const std::string& clientsJson){
 const auto clients=nlohmann::json::parse(clientsJson);nlohmann::json rows=nlohmann::json::array();
 for(const auto& entry:clients.items()){
  const auto colon=entry.key().find(':');if(colon==std::string::npos)continue;
  const auto user=number<int>(std::optional<std::string>(entry.key().substr(0,colon)));if(!user||*user<=0)continue;
  const auto activity=entry.value().value("activity",nlohmann::json::object());
  for(const auto& day:activity.items()){const auto& value=day.value();if(value.value("created",0LL)+value.value("updated",0LL)+value.value("deleted",0LL)==0)continue;
   rows.push_back({{"user_id",*user},{"client_key",entry.key()},{"day",day.key()},{"created",value.value("created",0LL)},{"updated",value.value("updated",0LL)},{"deleted",value.value("deleted",0LL)}});
  }
 }
 if(rows.empty())return true;
 SqlCommand sql(R"SQL(SET XACT_ABORT ON; BEGIN TRANSACTION;
 MERGE dbo.user_canvas_activity WITH(HOLDLOCK) AS target
 USING (SELECT s.* FROM OPENJSON(@rows) WITH(user_id int '$.user_id',client_key varchar(160) '$.client_key',activity_date date '$.day',created_count bigint '$.created',updated_count bigint '$.updated',deleted_count bigint '$.deleted') s JOIN dbo.users u WITH(UPDLOCK,HOLDLOCK) ON u.user_id=s.user_id AND u.status='ACTIVE' AND (u.password_hash IS NULL OR u.password_hash NOT LIKE 'bot-sha256:%')) AS source
 ON target.user_id=source.user_id AND target.canvas_id=@canvas AND target.client_key=source.client_key AND target.activity_date=source.activity_date
 WHEN MATCHED AND (source.created_count>target.created_count OR source.updated_count>target.updated_count OR source.deleted_count>target.deleted_count)
 THEN UPDATE SET created_count=source.created_count,updated_count=source.updated_count,deleted_count=source.deleted_count,updated_at=SYSUTCDATETIME()
 WHEN NOT MATCHED THEN INSERT(user_id,canvas_id,client_key,activity_date,created_count,updated_count,deleted_count) VALUES(source.user_id,@canvas,source.client_key,source.activity_date,source.created_count,source.updated_count,source.deleted_count);
 COMMIT TRANSACTION;)SQL");
 sql.addInt("@canvas",canvasId).addMaxText("@rows",rows.dump());return storage_->execute(sql);
}
