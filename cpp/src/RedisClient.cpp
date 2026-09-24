#include "RedisClient.hpp"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#include <cctype>

namespace {
std::string envOr(const char* name, const std::string& value) {
    if (!value.empty()) return value;
    const char* configured = std::getenv(name);
    return configured ? configured : "";
}

bool isWrongTypeResponse(const std::string& response) {
    std::string normalized = response;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized.find("wrongtype") != std::string::npos
        || normalized.find("wrong redis type") != std::string::npos;
}
}

RedisClient::RedisClient(const std::string& host, int port, const std::string& user, const std::string& password)
    : host_(host), port_(port), user_(envOr("REDIS_USER", user)),
      password_(envOr("REDIS_USER_PASSWORD", password)), socket_fd_(-1) {
}

RedisClient::~RedisClient() {
    disconnect();
}

bool RedisClient::connect() {
    if (socket_fd_ >= 0) {
        return true;
    }

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "[RedisClient] Failed to create socket\n";
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[RedisClient] Invalid address: " << host_ << "\n";
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    int original_flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, original_flags | O_NONBLOCK);
    int connect_result = ::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (connect_result < 0 && errno == EINPROGRESS) {
        pollfd pfd{socket_fd_, POLLOUT, 0};
        connect_result = poll(&pfd, 1, 3000);
        if (connect_result > 0) {
            int socket_error = 0;
            socklen_t socket_error_len = sizeof(socket_error);
            getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len);
            connect_result = socket_error == 0 ? 0 : -1;
        } else {
            connect_result = -1;
        }
    }
    fcntl(socket_fd_, F_SETFL, original_flags);
    if (connect_result < 0) {
        std::cerr << "[RedisClient] Connect failed to " << host_ << ":" << port_ << "\n";
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (!password_.empty()) {
        if (!user_.empty()) {
            sendCommand({"AUTH", user_, password_});
        } else {
            sendCommand({"AUTH", password_});
        }
        std::string auth_res = readResponse();
        if (auth_res != "OK") {
            std::cerr << "[RedisClient] Authentication failed: " << auth_res << "\n";
            disconnect();
            return false;
        }
    } else {
        std::cerr << "[RedisClient] REDIS_USER_PASSWORD is not configured\n";
        disconnect();
        return false;
    }

    return true;
}

void RedisClient::disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool RedisClient::sendCommand(const std::vector<std::string>& args) {
    if (socket_fd_ < 0 && !connect()) {
        return false;
    }

    std::ostringstream oss;
    oss << "*" << args.size() << "\r\n";
    for (const auto& arg : args) {
        oss << "$" << arg.length() << "\r\n" << arg << "\r\n";
    }

    std::string msg = oss.str();
    std::size_t total = 0;
    while (total < msg.size()) {
        ssize_t sent = write(socket_fd_, msg.data() + total, msg.size() - total);
        if (sent <= 0) {
            disconnect();
            return false;
        }
        total += static_cast<std::size_t>(sent);
    }
    return true;
}

std::string RedisClient::readLine() {
    std::string line;
    char c;
    while (read(socket_fd_, &c, 1) == 1) {
        if (c == '\r') {
            char next_c;
            if (read(socket_fd_, &next_c, 1) == 1 && next_c == '\n') {
                break;
            }
        } else {
            line += c;
        }
    }
    return line;
}

std::string RedisClient::readResponse() {
    if (socket_fd_ < 0) return "";
    std::string prefix = readLine();
    if (prefix.empty()) return "";

    char type = prefix[0];
    if (type == '+' || type == '-' || type == ':') {
        return prefix.substr(1);
    } else if (type == '$') {
        int len = std::stoi(prefix.substr(1));
        if (len == -1) return "";
        std::vector<char> buf(len);
        ssize_t total = 0;
        while (total < len) {
            ssize_t r = read(socket_fd_, buf.data() + total, len - total);
            if (r <= 0) break;
            total += r;
        }
        // consume \r\n
        char crlf[2];
        ssize_t crlf_total = 0;
        while (crlf_total < 2) {
            ssize_t r = read(socket_fd_, crlf + crlf_total, 2 - crlf_total);
            if (r <= 0) break;
            crlf_total += r;
        }
        if (total != len || crlf_total != 2) {
            disconnect();
            return "";
        }
        return std::string(buf.data(), static_cast<std::size_t>(total));
    } else if (type == '*') {
        int count = std::stoi(prefix.substr(1));
        std::string result;
        for (int i = 0; i < count; ++i) {
            std::string item = readResponse();
            if (i > 0) result += " ";
            result += item;
        }
        return result;
    }
    return prefix;
}

bool RedisClient::ping() {
    if (!sendCommand({"PING"})) return false;
    std::string res = readResponse();
    return res == "PONG";
}

bool RedisClient::set(const std::string& key, const std::string& value) {
    if (!sendCommand({"JSON.SET", key, "$", value})) return false;
    std::string res = readResponse();
    if (isWrongTypeResponse(res)) {
        if (!del(key) || !sendCommand({"JSON.SET", key, "$", value})) return false;
        res = readResponse();
    }
    return res == "OK";
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    if (!sendCommand({"JSON.GET", key})) return std::nullopt;
    std::string res = readResponse();
    if (isWrongTypeResponse(res)) {
        if (!sendCommand({"GET", key})) return std::nullopt;
        res = readResponse();
    }
    if (res.empty()) return std::nullopt;
    return res;
}

std::optional<std::string> RedisClient::getJsonPath(const std::string& key, const std::string& path) {
    if (!sendCommand({"JSON.GET", key, path})) return std::nullopt;
    const std::string response = readResponse();
    if (response.empty() || response.rfind("ERR", 0) == 0) return std::nullopt;
    return response;
}

bool RedisClient::setJsonPath(const std::string& key, const std::string& path, const nlohmann::json& value) {
    if (!sendCommand({"JSON.SET", key, path, value.dump()})) return false;
    return readResponse() == "OK";
}

bool RedisClient::appendChatMessage(const std::string& key, const std::string& item_id,
                                   std::uint64_t sequence, const nlohmann::json& message) {
    std::string escaped_id;
    escaped_id.reserve(item_id.size());
    for (char ch : item_id) {
        if (ch == '\\' || ch == '"') escaped_id.push_back('\\');
        escaped_id.push_back(ch);
    }
    const std::string room_path = "$[\"items\"][\"" + escaped_id + "\"]";
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
    if (!sendCommand({"EVAL", script, "1", key, type_path, data_path, sequence_path,
                      std::to_string(sequence), message.dump()})) return false;
    return readResponse() == "OK";
}

std::optional<std::string> RedisClient::getChatHistoryPage(
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
    if (!sendCommand({"EVAL", script, "1", key, room_path, type_path, data_path,
                      from_sequence ? std::to_string(*from_sequence) : std::string("0"),
                      to_sequence ? std::to_string(*to_sequence) : std::string("0"),
                      std::to_string(limit), mode})) return std::nullopt;
    const std::string response = readResponse();
    if (response.empty() || response.rfind("ERR", 0) == 0) return std::nullopt;
    return response;
}

RedisClient::CompareSetResult RedisClient::compareAndSetJsonPaths(
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
    if (!sendCommand(command)) return CompareSetResult::Error;
    const std::string result = readResponse();
    if (result == "OK") return CompareSetResult::Applied;
    if (result == "CONFLICT") return CompareSetResult::Conflict;
    return CompareSetResult::Error;
}

bool RedisClient::deleteJsonPath(const std::string& key, const std::string& path) {
    if (!sendCommand({"JSON.DEL", key, path})) return false;
    const std::string response = readResponse();
    return !response.empty() && response.find("ERR") == std::string::npos;
}

bool RedisClient::del(const std::string& key) {
    if (!sendCommand({"DEL", key})) return false;
    const std::string response = readResponse();
    try { return std::stoi(response) >= 0; } catch (...) { return false; }
}

bool RedisClient::deletePattern(const std::string& pattern) {
    if (!sendCommand({"KEYS", pattern})) return false;
    std::string keys_str = readResponse();
    if (keys_str.rfind("ERR", 0) == 0) return false;
    if (keys_str.empty()) return true;

    std::istringstream iss(keys_str);
    std::string key;
    std::vector<std::string> del_args = {"DEL"};
    while (iss >> key) {
        del_args.push_back(key);
    }
    if (del_args.size() > 1) {
        if (!sendCommand(del_args)) return false;
        const std::string response = readResponse();
        try { return std::stoi(response) >= 0; } catch (...) { return false; }
    }
    return true;
}

int RedisClient::getKeyCount(const std::string& pattern) {
    if (!sendCommand({"KEYS", pattern})) return 0;
    std::string keys_str = readResponse();
    if (keys_str.empty()) return 0;
    std::istringstream iss(keys_str);
    std::string key;
    int count = 0;
    while (iss >> key) count++;
    return count;
}

bool RedisClient::updateJson(const std::string& key, const std::function<void(nlohmann::json&)>& modifier) {
    auto current = get(key);
    nlohmann::json root;
    if (current && !current->empty()) {
        try {
            root = nlohmann::json::parse(*current);
        } catch (...) {
            root = nlohmann::json::object();
        }
    } else {
        root = nlohmann::json::object();
    }

    modifier(root);
    return set(key, root.dump());
}
