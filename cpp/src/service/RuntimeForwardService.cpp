#include "service/RuntimeForwardService.hpp"

#include "Environment.hpp"
#include "service/CanvasAccessRules.hpp"
#include "service/CanvasGroupBits.hpp"
#include "service/CanvasQueryRules.hpp"

#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <climits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace {
using Json = nlohmann::json;
constexpr std::size_t MAX_REQUEST_BYTES = 800000;
constexpr std::size_t MAX_FRAME_BYTES = 400000;
constexpr std::size_t MAX_STREAM_OUTPUT_BYTES = 196608;

void error(HttpApi::Response& response, int status, const std::string& code, const std::string& message) {
    response.status = status;
    response.set_content(Json{{"error", code}, {"message", message}}.dump(), "application/json; charset=utf-8");
}

bool hasOnlyKeys(const Json& value, std::initializer_list<const char*> allowed) {
    if (!value.is_object()) return false;
    std::set<std::string> keys;
    for (const char* key : allowed) keys.emplace(key);
    for (const auto& entry : value.items()) if (!keys.count(entry.key())) return false;
    return true;
}

bool validIdentifier(const Json& value) {
    if (!value.is_string()) return false;
    const auto id = value.get<std::string>();
    return !id.empty() && id.size() <= 128 && id != "__proto__" && id != "prototype" && id != "constructor"
        && std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}

bool validateRequest(const Json& input, bool stream, std::string& reason) {
    if (!input.is_object() || input.dump().size() > MAX_REQUEST_BYTES) { reason = "실행 요청 크기가 허용 범위를 넘었습니다."; return false; }
    if ((input.contains("action") && !input["action"].is_string())
        || (input.contains("stream") && !input["stream"].is_boolean())) { reason = "실행 요청 형식이 올바르지 않습니다."; return false; }
    for (const char* required : {"canvasId", "userId", "settingsRevision", "code"})
        if (!input.contains(required)) { reason = "브로커 권한 확인 정보가 누락되었습니다."; return false; }
    if (!input["canvasId"].is_number_integer() || input["canvasId"].get<long long>() <= 0 || input["canvasId"].get<long long>() > INT_MAX
        || !input["userId"].is_number_integer() || input["userId"].get<long long>() <= 0 || input["userId"].get<long long>() > INT_MAX
        || !input["settingsRevision"].is_number_integer() || input["settingsRevision"].get<long long>() < 0
        || !input["code"].is_string() || input["code"].get_ref<const std::string&>().size() > 65536) {
        reason = "실행 요청의 사용자, 캔버스 또는 코드 형식이 올바르지 않습니다."; return false;
    }
    if (input.contains("scope")) {
        if (!input["scope"].is_string()) { reason = "실행 공간 범위 형식이 올바르지 않습니다."; return false; }
        try {
            (void)parseSpaceScope(input["scope"].get<std::string>());
        } catch (const std::invalid_argument&) {
            reason = "지원하지 않는 실행 공간 범위입니다.";
            return false;
        }
    }
    const auto action = input.value("action", std::string("execute"));
    if (action == "execute") {
        if (!hasOnlyKeys(input, {"canvasId", "scope", "userId", "settingsRevision", "objectId", "language", "code", "stdin", "stream"})
            || !input.contains("language") || !input["language"].is_string()) { reason = "코드 실행 요청 형식이 올바르지 않습니다."; return false; }
        const auto language = input["language"].get<std::string>();
        if (language != "python" && language != "javascript" && language != "c" && language != "cpp" && language != "bash" && language != "java") {
            reason = "지원하지 않는 실행 언어입니다."; return false;
        }
        if (input.contains("stdin") && (!input["stdin"].is_string() || input["stdin"].get_ref<const std::string&>().size() > 65536)) {
            reason = "표준 입력은 64KB 이하여야 합니다."; return false;
        }
        if (input.contains("objectId") && !validIdentifier(input["objectId"])) { reason = "코드 객체 ID가 올바르지 않습니다."; return false; }
        if (stream != (input.value("stream", false))) { reason = "실행 스트림 경로가 요청과 일치하지 않습니다."; return false; }
        return true;
    }
    if (action == "build") {
        if (stream || !hasOnlyKeys(input, {"canvasId", "scope", "userId", "settingsRevision", "objectId", "action", "language", "code"})
            || !input.contains("language") || !input["language"].is_string()
            || (input.contains("objectId") && !validIdentifier(input["objectId"]))) {
            reason = "사전 빌드 요청 형식이 올바르지 않습니다."; return false;
        }
        const auto language = input["language"].get<std::string>();
        if (language != "python" && language != "javascript" && language != "c" && language != "cpp" && language != "bash" && language != "java") {
            reason = "지원하지 않는 빌드 언어입니다."; return false;
        }
        return true;
    }
    if (action == "notebook") {
        if (stream || !hasOnlyKeys(input, {"canvasId", "scope", "userId", "settingsRevision", "objectId", "action", "operation", "notebook", "code"})
            || !input.contains("operation") || !input["operation"].is_string() || input["operation"] != "execute"
            || !input.contains("objectId") || !validIdentifier(input["objectId"])
            || !input.contains("notebook") || !validIdentifier(input["notebook"])) {
            reason = "주피터 셀 실행 요청 형식이 올바르지 않습니다."; return false;
        }
        return true;
    }
    reason = "허용되지 않은 VM 작업입니다.";
    return false;
}

bool authorizeWithBroker(CanvasLifecycleMemory& canvases, const Json& request, std::string& reason) {
    const SpaceScope scope = parseSpaceScope(request.value("scope", std::string("canvas")));
    const int canvas_id = request["canvasId"].get<int>();
    const int user_id = request["userId"].get<int>();
    const long long revision = request["settingsRevision"].get<long long>();
    auto document = canvases.getAuthorizedCanvasDocument(scope, canvas_id, user_id, revision);
    if (!document) { reason = "캔버스 참여 권한이 취소되었거나 설정이 변경되었습니다."; return false; }

    auto [execute_groups, admin] = groupsForUser(*document, user_id, "execute");
    if (execute_groups.empty() && !admin) { reason = "브로커가 이 캔버스의 실행 권한을 거부했습니다."; return false; }

    const auto action = request.value("action", std::string("execute"));
    const std::string object_id = action == "notebook"
        ? request.value("notebook", std::string())
        : request.value("objectId", std::string());
    if (!object_id.empty()) {
        const auto& items = document->value("items", Json::object());
        if (!items.is_object() || !items.contains(object_id) || !items[object_id].is_object()) {
            reason = "실행 대상 객체가 없거나 접근할 수 없습니다."; return false;
        }
        const auto& item = items[object_id];
        if (action == "notebook") {
            if (item.value("kind", std::string()) != "shape" || item.value("runtimeRole", std::string()) != "notebook") {
                reason = "주피터 노트북 실행 객체가 아닙니다."; return false;
            }
        } else if (!(item.value("kind", std::string()) == "code"
                     || (item.value("kind", std::string()) == "text" && item.value("renderMode", std::string()) == "code"))) {
            reason = "실행 가능한 코드 객체가 아닙니다."; return false;
        }
        CanvasAclScope acl(canvasGroupBitLayout(*document), {&execute_groups});
        if (!queryCanAccess(item, execute_groups, admin)) { reason = "브로커가 대상 객체의 실행 권한을 거부했습니다."; return false; }
    }
    return true;
}

class ManagerConnection {
public:
    explicit ManagerConnection(const std::string& path) {
        sockaddr_un address{};
        if (path.empty() || path.size() >= sizeof(address.sun_path)) throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
        fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd_ < 0) throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
        timeval timeout{240, 0};
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(); throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
        }
    }
    ~ManagerConnection() { close(); }
    ManagerConnection(const ManagerConnection&) = delete;
    ManagerConnection& operator=(const ManagerConnection&) = delete;

    void sendRequest(const Json& request) {
        const std::string line = request.dump() + "\n";
        if (line.size() > MAX_REQUEST_BYTES) throw std::runtime_error("RUNTIME_REQUEST_TOO_LARGE");
        std::size_t sent = 0;
        while (sent < line.size()) {
            const auto result = ::send(fd_, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
            if (result <= 0) throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
            sent += static_cast<std::size_t>(result);
        }
    }

    std::string readLine() {
        while (true) {
            const auto end = pending_.find('\n');
            if (end != std::string::npos) {
                std::string line = pending_.substr(0, end);
                pending_.erase(0, end + 1);
                return line;
            }
            if (pending_.size() >= MAX_FRAME_BYTES) throw std::runtime_error("RUNTIME_FRAME_TOO_LARGE");
            char buffer[8192];
            const auto count = ::recv(fd_, buffer, sizeof(buffer), 0);
            if (count <= 0) throw std::runtime_error("RUNTIME_VM_UNAVAILABLE");
            pending_.append(buffer, static_cast<std::size_t>(count));
        }
    }

    void close() {
        if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; }
    }

private:
    int fd_{-1};
    std::string pending_;
};

Json managerRequest(const Json& request) {
    Json result = request;
    const SpaceScope scope = parseSpaceScope(result.value("scope", std::string("canvas")));
    result["canvasId"] = runtimeTargetId(scope, result["canvasId"].get<int>());
    result.erase("scope");
    result.erase("userId");
    result.erase("settingsRevision");
    result.erase("objectId");
    if(result.value("action",std::string())=="execute")result.erase("action");
    return result;
}

void forwardOne(CanvasLifecycleMemory& canvases, const std::string& manager_socket,
                const HttpApi::Request& request, HttpApi::Response& response, bool stream) {
    Json input;
    try { input = Json::parse(request.body); }
    catch (...) { return error(response, 400, "RUNTIME_REQUEST_INVALID", "실행 요청 JSON을 확인하세요."); }
    std::string reason;
    if (!validateRequest(input, stream, reason)) return error(response, 400, "RUNTIME_REQUEST_INVALID", reason);
    if (!authorizeWithBroker(canvases, input, reason)) return error(response, 403, "RUNTIME_EXECUTION_DENIED", reason);

    Json payload = managerRequest(input);
    std::shared_ptr<ManagerConnection> connection;
    try {
        connection = std::make_shared<ManagerConnection>(manager_socket);
        connection->sendRequest(payload);
    } catch (const std::exception& exception) {
        const std::string code = exception.what();
        if (code == "RUNTIME_REQUEST_TOO_LARGE") return error(response, 400, "RUNTIME_REQUEST_INVALID", "실행 요청 크기가 허용 범위를 넘었습니다.");
        return error(response, 503, "RUNTIME_MANAGER_UNAVAILABLE", "C++ 브로커에서 VM 실행환경 관리자에 연결할 수 없습니다.");
    }

    if (!stream) {
        try {
            const auto line = connection->readLine();
            Json result = Json::parse(line);
            if (result.value("error", std::string()) == "RUNTIME_UNAVAILABLE")
                return error(response, 503, "RUNTIME_VM_UNAVAILABLE", "VM 워커에 코드를 전달하거나 실행 결과를 받지 못했습니다.");
            response.status = 200;
            response.set_content(result.dump(), "application/json; charset=utf-8");
        } catch (...) {
            error(response, 503, "RUNTIME_VM_UNAVAILABLE", "VM 워커 연결이 실행 도중 종료되었습니다.");
        }
        return;
    }

    auto output_bytes = std::make_shared<std::size_t>(0);
    auto complete = std::make_shared<bool>(false);
    response.set_header("Cache-Control", "no-store");
    response.set_header("X-Accel-Buffering", "no");
    response.set_chunked_content_provider("application/x-ndjson",
        [connection, output_bytes, complete](std::size_t, httplib::DataSink& sink) mutable {
            if (*complete) { sink.done(); return true; }
            try {
                const std::string raw = connection->readLine();
                Json frame = Json::parse(raw);
                Json outgoing;
                if (frame.value("type", std::string()) == "output") {
                    if (!frame.contains("output") || !frame["output"].is_string()) throw std::runtime_error("INVALID_OUTPUT_FRAME");
                    *output_bytes += frame["output"].get_ref<const std::string&>().size();
                    if (*output_bytes > MAX_STREAM_OUTPUT_BYTES) throw std::runtime_error("RUNTIME_OUTPUT_TOO_LARGE");
                    outgoing = std::move(frame);
                } else {
                    *complete = true;
                    if (frame.contains("error")) {
                        outgoing = {{"type", "error"}, {"code", frame.value("error", "RUNTIME_VM_UNAVAILABLE")},
                                    {"message", frame.value("message", std::string("VM 워커 실행이 실패했습니다."))}};
                    } else outgoing = {{"type", "result"}, {"result", std::move(frame)}};
                }
                const std::string line = outgoing.dump() + "\n";
                if (!sink.write(line.data(), line.size())) { connection->close(); return false; }
                if (*complete) { sink.done(); connection->close(); }
                return true;
            } catch (const std::exception& exception) {
                *complete = true;
                Json failure{{"type", "error"}, {"code", std::string(exception.what()) == "RUNTIME_OUTPUT_TOO_LARGE" ? "RUNTIME_OUTPUT_TOO_LARGE" : "RUNTIME_VM_UNAVAILABLE"},
                             {"message", "VM 워커의 실행 결과 스트림을 처리하지 못했습니다."}};
                const std::string line = failure.dump() + "\n";
                sink.write(line.data(), line.size());
                sink.done(); connection->close(); return true;
            }
        },
        [connection](bool) { connection->close(); });
}
}

void RuntimeForwardService::registerRoutes(HttpApi& api) {
    const auto socket = manager_socket_.empty()
        ? environmentValue("AGORA_RUNTIME_SOCKET")
        : manager_socket_;
    const auto manager_socket = socket.empty() ? std::string("/run/agora-runtime/manager.sock") : socket;
    api.post("/api/runtime/forward", [this, manager_socket](const HttpApi::Request& request, HttpApi::Response& response) {
        forwardOne(canvases_, manager_socket, request, response, false);
    });
    api.post("/api/runtime/forward/stream", [this, manager_socket](const HttpApi::Request& request, HttpApi::Response& response) {
        forwardOne(canvases_, manager_socket, request, response, true);
    });
}
