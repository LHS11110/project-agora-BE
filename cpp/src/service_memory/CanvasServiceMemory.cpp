#include "service_memory/CanvasServiceMemory.hpp"
#include "service_memory/CanvasSnapshotMemory.hpp"
#include "memory/MemoryClass.hpp"
#include "CanvasPassword.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
namespace {
std::size_t configuredCacheCapacity(const char* variable, std::size_t default_capacity,
                                    std::size_t maximum_capacity, bool allow_zero) {
    const char* configured = std::getenv(variable);
    if (!configured || !*configured) return default_capacity;
    for (const char* digit = configured; *digit; ++digit) {
        if (!std::isdigit(static_cast<unsigned char>(*digit))) {
            return default_capacity;
        }
    }
    try {
        std::size_t parsed_length = 0;
        const auto parsed = std::stoull(configured, &parsed_length);
        if (parsed_length != std::string(configured).size() || (!allow_zero && parsed == 0)) {
            return default_capacity;
        }
        return std::min<std::size_t>(parsed, maximum_capacity);
    } catch (...) {
        return default_capacity;
    }
}

std::shared_ptr<MemoryClass::State> canvasMemoryState(const std::string& backend) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::shared_ptr<MemoryClass::State>> states;
    std::lock_guard<std::mutex> lock(mutex);
    auto& state = states[backend];
    if (!state) {
        MemoryClass::Options options;
        options.maximum_documents = configuredCacheCapacity("CPP_CANVAS_LRU_CAPACITY", 256, 4096, false);
        options.entries_per_document = configuredCacheCapacity("CPP_CANVAS_LRU_ITEMS_PER_CANVAS", 64, 4096, true);
        options.region_depth = 2;
        options.remote_fields = [](const nlohmann::json& value) {
            return value.is_object() && value.value("type", nlohmann::json()) == "chat_room"
                ? std::vector<std::string>{"data"} : std::vector<std::string>{};
        };
        state = MemoryClass::createState(std::move(options));
    }
    return state;
}
std::string canvasItemJsonPath(const std::string& id) {
    return "$[\"items\"][" + nlohmann::json(id).dump() + "]";
}
bool isRedisErrorResponse(const std::string& reply) { return !reply.empty() && reply.front() == '\x01'; }
}
CanvasServiceMemory::CanvasServiceMemory(const std::string& host, int port, const std::string& user, const std::string& password)
    : memory_(std::make_unique<MemoryClass>(host, port, user, password,
        canvasMemoryState(host + ":" + std::to_string(port) + ":" + user))) {}
CanvasServiceMemory::CanvasServiceMemory(std::unique_ptr<RedisCommandExecutor> transport)
    : memory_(std::make_unique<MemoryClass>(std::move(transport), canvasMemoryState("injected"))) {}
CanvasServiceMemory::CanvasServiceMemory(std::unique_ptr<MemoryClass> memory) : memory_(std::move(memory)) {
    if (!memory_) throw std::invalid_argument("memory is required");
}
CanvasServiceMemory::~CanvasServiceMemory() = default;
bool CanvasServiceMemory::connect() { return memory_->connect(); }
void CanvasServiceMemory::disconnect() { memory_->disconnect(); }
bool CanvasServiceMemory::ping() { return memory_->ping(); }
bool CanvasServiceMemory::appendChatMessage(const std::string& key, const std::string& item_id,
                                   std::uint64_t sequence, const nlohmann::json& message) {
    const std::string room_path = canvasItemJsonPath(item_id);
    const std::string type_path = room_path + "[\"type\"]";
    const std::string data_path = room_path + "[\"data\"]";
    const std::string sequence_path = room_path + "[\"next_sequence\"]";
    static const std::string script = R"LUA(
local type_raw = redis.call('JSON.GET', KEYS[1], ARGV[1])
if not type_raw then return 'MISSING_ROOM' end
local types = cjson.decode(type_raw)
if types[1] ~= 'chat_room' then return 'NOT_CHAT_ROOM' end

local lengths_ok, lengths = pcall(redis.call, 'JSON.ARRLEN', KEYS[1], ARGV[2])
if not lengths_ok then return 'BAD_HISTORY' end
local length_value = type(lengths) == 'table' and lengths[1] or lengths
local data_length = tonumber(length_value)
if not data_length then
    redis.call('JSON.SET', KEYS[1], ARGV[2], '[]')
    data_length = 0
end

local next_raw = redis.call('JSON.GET', KEYS[1], ARGV[3])
local next_sequence = nil
if next_raw then
    local next_matches = cjson.decode(next_raw)
    next_sequence = tonumber(next_matches[1])
end
if not next_sequence then
    local history_raw = redis.call('JSON.GET', KEYS[1], ARGV[2])
    local maximum = 0
    if history_raw then
        local history_matches = cjson.decode(history_raw)
        local history = history_matches[1]
        if type(history) ~= 'table' then return 'BAD_HISTORY' end
        for _, previous in ipairs(history) do
            if type(previous) == 'table' then
                local previous_sequence = tonumber(previous.sequence)
                if previous_sequence and previous_sequence > maximum then maximum = previous_sequence end
            end
        end
    end
    next_sequence = maximum + 1
end
local expected = tonumber(ARGV[4])
if next_sequence ~= expected then return 'SEQUENCE_CONFLICT' end

local message = cjson.decode(ARGV[5])
if tonumber(message.sequence) ~= expected then return 'BAD_MESSAGE' end
local appended = redis.call('JSON.ARRAPPEND', KEYS[1], ARGV[2], ARGV[5])
if not appended then return 'APPEND_FAILED' end
redis.call('JSON.SET', KEYS[1], ARGV[3], tostring(expected + 1))
return 'OK'
)LUA";
    return memory_->executeAtomic(key, {"EVAL", script, "1", key, type_path, data_path, sequence_path,
        std::to_string(sequence), message.dump()}, {room_path}) == "OK";
}

std::optional<std::string> CanvasServiceMemory::getChatHistoryPage(
        const std::string& key, const std::string& item_id,
        const std::optional<std::uint64_t>& from_sequence,
        const std::optional<std::uint64_t>& to_sequence, std::uint64_t limit) {
    std::string escaped_id;
    escaped_id.reserve(item_id.size());
    for (char ch : item_id) {
        if (ch == '\\' || ch == '"') escaped_id.push_back('\\');
        escaped_id.push_back(ch);
    }
    const std::string room_path = "$[\"items\"][\"" + escaped_id + "\"]";
    const std::string type_path = room_path + "[\"type\"]";
    const std::string data_path = room_path + "[\"data\"]";
    const std::string mode = from_sequence && to_sequence ? "range"
        : from_sequence ? "from" : to_sequence ? "to" : "latest";
    static const std::string script = R"LUA(
local type_raw = redis.call('JSON.GET', KEYS[1], ARGV[2])
if not type_raw then return 'ROOM_NOT_FOUND' end
local types = cjson.decode(type_raw)
if types[1] ~= 'chat_room' then return 'ROOM_NOT_FOUND' end

local lengths_ok, lengths = pcall(redis.call, 'JSON.ARRLEN', KEYS[1], ARGV[3])
if not lengths_ok then return 'BAD_HISTORY' end
local length_value = type(lengths) == 'table' and lengths[1] or lengths
local total = tonumber(length_value) or 0
local requested_from = tonumber(ARGV[4]) or 0
local requested_to = tonumber(ARGV[5]) or 0
local requested_limit = tonumber(ARGV[6]) or 50
local mode = ARGV[7]
local start_index = 0
local end_index = total
local has_more = false

if mode == 'latest' then
    start_index = math.max(0, total - requested_limit)
    has_more = start_index > 0
elseif mode == 'from' then
    start_index = math.min(total, requested_from - 1)
    end_index = math.min(total, start_index + requested_limit)
    has_more = end_index < total
elseif mode == 'to' then
    end_index = math.min(total, requested_to)
    start_index = math.max(0, end_index - requested_limit)
    has_more = start_index > 0
else
    start_index = math.min(total, requested_from - 1)
    end_index = math.min(total, requested_to)
    if end_index < start_index then end_index = start_index end
end

local slice_path = ARGV[3] .. '[' .. start_index .. ':' .. end_index .. ']'
local slice_ok, slice_raw = pcall(redis.call, 'JSON.GET', KEYS[1], slice_path)
if not slice_ok then slice_raw = nil end
local messages = slice_raw and cjson.decode(slice_raw) or {}
local contiguous = type(messages) == 'table' and #messages == end_index - start_index
if contiguous then
    for i, message in ipairs(messages) do
        if type(message) ~= 'table' or tonumber(message.sequence) ~= start_index + i then
            contiguous = false
            break
        end
    end
end

if not contiguous then
    local raw = redis.call('JSON.GET', KEYS[1], ARGV[1])
    if not raw then return 'ROOM_NOT_FOUND' end
    local matches = cjson.decode(raw)
    local room = matches[1]
    local data = room.data or {}
    if type(data) ~= 'table' then return 'BAD_HISTORY' end
    local rows = {}
    for _, message in ipairs(data) do
        if type(message) == 'table' then
            local sequence = tonumber(message.sequence)
            if sequence and sequence > 0 then
                rows[#rows + 1] = {sequence = sequence, message = message}
            end
        end
    end
    table.sort(rows, function(left, right) return left.sequence < right.sequence end)
    local selected = {}
    total = #rows
    if mode == 'latest' then
        local first = math.max(1, #rows - requested_limit + 1)
        has_more = first > 1
        for i = first, #rows do selected[#selected + 1] = rows[i] end
    elseif mode == 'from' then
        local candidates = {}
        for _, row in ipairs(rows) do
            if row.sequence >= requested_from then candidates[#candidates + 1] = row end
        end
        local count = math.min(#candidates, requested_limit)
        has_more = #candidates > count
        for i = 1, count do selected[#selected + 1] = candidates[i] end
    elseif mode == 'to' then
        local candidates = {}
        for _, row in ipairs(rows) do
            if row.sequence <= requested_to then candidates[#candidates + 1] = row end
        end
        local first = math.max(1, #candidates - requested_limit + 1)
        has_more = first > 1
        for i = first, #candidates do selected[#selected + 1] = candidates[i] end
    else
        for _, row in ipairs(rows) do
            if row.sequence >= requested_from and row.sequence <= requested_to then
                selected[#selected + 1] = row
            end
        end
    end
    messages = {}
    for _, row in ipairs(selected) do messages[#messages + 1] = row.message end
    start_index = selected[1] and selected[1].sequence - 1 or 0
    end_index = selected[#selected] and selected[#selected].sequence or 0
end

local response = {total = total, messages = messages, has_more = has_more}
if #messages > 0 then
    response.from_sequence = tonumber(messages[1].sequence) or start_index + 1
    response.to_sequence = tonumber(messages[#messages].sequence) or end_index
end
    return cjson.encode(response)
)LUA";
    auto reply = memory_->executeAtomic(key, {"EVAL", script, "1", key, room_path, type_path, data_path,
                      from_sequence ? std::to_string(*from_sequence) : std::string("0"),
                      to_sequence ? std::to_string(*to_sequence) : std::string("0"),
                      std::to_string(limit), mode}, {});
    if (!reply) return std::nullopt;
    const std::string response = *reply;
    if (response.empty() || isRedisErrorResponse(response)) return std::nullopt;
    return response;
}

CanvasServiceMemory::CompareSetResult CanvasServiceMemory::compareAndSetJsonPaths(
        const std::string& key, long long expected_revision,
        const std::vector<std::pair<std::string, nlohmann::json>>& values,
        const std::vector<std::string>& deletes) {
    if (values.empty() && deletes.empty()) return CompareSetResult::Error;
    static const std::string script = R"LUA(
local raw = redis.call('JSON.GET', KEYS[1], '$["settings-revision"]')
if redis.call('EXISTS', KEYS[1]) == 0 then return 'MISSING' end
local current = 0
if raw then
    local versions = cjson.decode(raw)
    current = tonumber(versions[1] or 0)
end
if current ~= tonumber(ARGV[1]) then return 'CONFLICT' end
local value_count = tonumber(ARGV[2])
local next_arg = 3
for i = next_arg, next_arg + value_count * 2 - 1, 2 do
    redis.call('JSON.SET', KEYS[1], ARGV[i], ARGV[i + 1])
end
local delete_count_index = next_arg + value_count * 2
local delete_count = tonumber(ARGV[delete_count_index])
for i = delete_count_index + 1, delete_count_index + delete_count do
    redis.call('JSON.DEL', KEYS[1], ARGV[i])
end
return 'OK'
)LUA";
    std::vector<std::string> command = {"EVAL", script, "1", key, std::to_string(expected_revision),
                                        std::to_string(values.size())};
    for (const auto& [path, value] : values) {
        command.push_back(path);
        command.push_back(value.dump());
    }
    command.push_back(std::to_string(deletes.size()));
    for (const auto& path : deletes) command.push_back(path);
    std::vector<std::string> changed = deletes;
    for (const auto& entry : values) changed.push_back(entry.first);
    const auto result = memory_->executeAtomic(key, command, changed);
    if (result == "OK") return CompareSetResult::Applied;
    if (result == "CONFLICT") return CompareSetResult::Conflict;
    return CompareSetResult::Error;
}

CanvasServiceMemory::CompareSetResult CanvasServiceMemory::deleteIfCacheGenerationMatches(
        const std::string& key, const std::string& generation) {
    static const std::string script = R"LUA(
local value = redis.call('JSON.GET', KEYS[1], '$["_cache_generation"]')
if not value then return 'CONFLICT' end
local decoded = cjson.decode(value)
if decoded[1] ~= ARGV[1] then return 'CONFLICT' end
redis.call('DEL', KEYS[1])
return 'APPLIED'
)LUA";
    auto response = memory_->executeAtomic(key, {"EVAL", script, "1", key, generation}, {"$"});
    if (response == "APPLIED") return CompareSetResult::Applied;
    if (response == "CONFLICT") return CompareSetResult::Conflict;
    return CompareSetResult::Error;
}

namespace {
std::string canvasKey(int id) { return "canvas:" + std::to_string(id); }
std::string fieldPath(const std::string& field) {
    return "$[" + nlohmann::json(field).dump() + "]";
}
}

std::optional<std::string> CanvasServiceMemory::loadCanvas(int id) { return memory_->read(canvasKey(id)); }
bool CanvasServiceMemory::storeCanvas(int id, const nlohmann::json& document) {
    return memory_->write(canvasKey(id), document);
}
bool CanvasServiceMemory::flushCanvas(int id) { return memory_->flush(canvasKey(id)); }
bool CanvasServiceMemory::clearCanvas(int id) {
    return memory_->erasePattern(canvasKey(id) + ":*") && memory_->erase(canvasKey(id));
}
CanvasServiceMemory::CompareSetResult CanvasServiceMemory::removeCanvasGeneration(int id, const std::string& generation) {
    return deleteIfCacheGenerationMatches(canvasKey(id), generation);
}
std::optional<std::string> CanvasServiceMemory::readItem(int id, const std::string& item_id, const std::string& field) {
    auto path = canvasItemJsonPath(item_id);
    if (!field.empty()) path += fieldPath(field).substr(1);
    return memory_->read(canvasKey(id), path);
}
bool CanvasServiceMemory::storeItem(int id, const std::string& item_id, const nlohmann::json& item) {
    return memory_->write(canvasKey(id), item, canvasItemJsonPath(item_id));
}
bool CanvasServiceMemory::removeItem(int id, const std::string& item_id) {
    return memory_->erase(canvasKey(id), canvasItemJsonPath(item_id));
}
bool CanvasServiceMemory::replaceItems(int id, const nlohmann::json& items) {
    return memory_->write(canvasKey(id), items, "$.items");
}
bool CanvasServiceMemory::appendMessage(int id, const std::string& room, std::uint64_t sequence, const nlohmann::json& message) {
    return appendChatMessage(canvasKey(id), room, sequence, message);
}
std::optional<std::string> CanvasServiceMemory::readChatPage(int id, const std::string& room,
        const std::optional<std::uint64_t>& from, const std::optional<std::uint64_t>& to, std::uint64_t limit) {
    return getChatHistoryPage(canvasKey(id), room, from, to, limit);
}
CanvasServiceMemory::CompareSetResult CanvasServiceMemory::compareAndSetFields(int id, long long revision,
        const std::vector<std::pair<std::string, nlohmann::json>>& fields) {
    std::vector<std::pair<std::string, nlohmann::json>> paths;
    paths.reserve(fields.size());
    for (const auto& [name, value] : fields) paths.emplace_back(fieldPath(name), value);
    return compareAndSetJsonPaths(canvasKey(id), revision, paths);
}

bool CanvasServiceMemory::patchItemField(int id, const std::string& item_id, const std::string& field,
                                       const nlohmann::json& value) {
    return memory_->write(canvasKey(id), value, canvasItemJsonPath(item_id) + fieldPath(field).substr(1));
}
bool CanvasServiceMemory::clearItems(int id) {
    return memory_->erase(canvasKey(id), "$.items");
}
std::optional<nlohmann::json> CanvasServiceMemory::initializeCanvas(int id, CanvasSnapshotMemory& snapshots,
        bool already_cached, const std::string& generation) {
    std::optional<nlohmann::json> document;
    if (already_cached) {
        auto serialized = loadCanvas(id);
        if (!serialized) return {};
        try { document = nlohmann::json::parse(*serialized); } catch (...) { return {}; }
    } else document = snapshots.getCanvasDocument(id);
    if (!document || !document->is_object()) return {};
    bool migrated = false;
    for (const char* legacy : {"canvas-password", "canvasPassword", "canvas_password_hash"}) {
        if (!document->contains(legacy)) continue;
        if (!document->contains("canvas-password-hash")) (*document)["canvas-password-hash"] = (*document)[legacy];
        document->erase(legacy); migrated = true;
    }
    if (document->contains("canvas-password-hash") && (*document)["canvas-password-hash"].is_string()) {
        auto normalized = normalizeCanvasPassword((*document)["canvas-password-hash"].get<std::string>());
        if (!normalized) return {};
        migrated |= (*document)["canvas-password-hash"] != *normalized;
        (*document)["canvas-password-hash"] = *normalized;
    }
    if (migrated && !snapshots.saveCanvasDocument(id, *document)) return {};
    (*document)["_cache_generation"] = generation;
    if (!storeCanvas(id, *document)) return {};
    return document;
}
