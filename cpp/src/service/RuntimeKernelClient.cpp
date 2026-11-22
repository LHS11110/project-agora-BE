#include "service/RuntimeKernelClient.hpp"

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
constexpr std::size_t MAX_REQUEST_BYTES = 800000;
constexpr std::size_t MAX_RESPONSE_BYTES = 400000;
constexpr std::size_t MAX_TOOL_FRAME_BYTES = 1000000;
constexpr int MAX_RUNTIME_TOOL_CALLS = 80;

nlohmann::json exchangeSocket(
    const std::string& socket_path,
    const nlohmann::json& payload,
    std::size_t max_request_bytes,
    std::size_t max_response_bytes,
    const std::function<nlohmann::json(const std::string&, const nlohmann::json&)>& on_tool,
    const std::function<void(const std::string&)>& on_output) {
    sockaddr_un address{};
    if (socket_path.empty() || socket_path.size() >= sizeof(address.sun_path))
        throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
    struct Guard {
        int fd;
        ~Guard() {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
    } guard{fd};

    timeval timeout{240, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeval send_timeout{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");

    const auto sendFrame = [&](const nlohmann::json& value, const char* unavailable_code) {
        const std::string encoded = value.dump() + "\n";
        if (encoded.size() > max_request_bytes)
            throw std::runtime_error("RUNTIME_REQUEST_TOO_LARGE");
        std::size_t sent = 0;
        while (sent < encoded.size()) {
            const auto count = ::send(fd, encoded.data() + sent, encoded.size() - sent, MSG_NOSIGNAL);
            if (count <= 0) throw std::runtime_error(unavailable_code);
            sent += static_cast<std::size_t>(count);
        }
    };

    sendFrame(payload, "RUNTIME_MANAGER_UNAVAILABLE");

    std::string response;
    char buffer[8192];
    int tool_calls = 0;
    while (true) {
        const auto newline = response.find('\n');
        if (newline != std::string::npos) {
            const auto line = response.substr(0, newline);
            response.erase(0, newline + 1);
            nlohmann::json frame;
            try {
                frame = nlohmann::json::parse(line);
            } catch (...) {
                throw std::runtime_error("RUNTIME_VM_UNAVAILABLE");
            }
            if (!frame.is_object()) throw std::runtime_error("RUNTIME_VM_UNAVAILABLE");
            const auto ftype = frame.value("type", std::string());
            if (ftype == "output") {
                if (on_output && frame.contains("output") && frame["output"].is_string()) {
                    const auto& text = frame["output"].get_ref<const std::string&>();
                    on_output(on_tool ? text.substr(0, 4000) : text);
                }
                continue;
            }
            if (ftype == "tool") {
                if (!on_tool || !frame.contains("id") || !frame.contains("name") || !frame["name"].is_string())
                    throw std::runtime_error("RUNTIME_VM_UNAVAILABLE");
                if (++tool_calls > MAX_RUNTIME_TOOL_CALLS)
                    throw std::runtime_error("AGENT_TOOL_LIMIT");
                nlohmann::json tool_result;
                try {
                    const auto args = frame.contains("arguments") && frame["arguments"].is_object()
                        ? frame["arguments"]
                        : nlohmann::json::object();
                    tool_result = on_tool(frame["name"].get<std::string>(), args);
                } catch (const std::exception&) {
                    tool_result = {{"error", "AGENT_TOOL_REJECTED"}, {"message", "도구 요청이 거부되었습니다."}};
                }
                sendFrame({{"type", "tool_result"}, {"id", frame["id"]}, {"result", tool_result}},
                          "RUNTIME_VM_UNAVAILABLE");
                continue;
            }
            return frame;
        }
        if (response.size() >= max_response_bytes)
            throw std::runtime_error("RUNTIME_RESPONSE_TOO_LARGE");
        const auto count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            throw std::runtime_error("RUNTIME_VM_UNAVAILABLE");
        response.append(buffer, static_cast<std::size_t>(count));
    }
}
}  // namespace

nlohmann::json RuntimeKernelClient::request(const nlohmann::json& payload,
                                             const std::function<void(const std::string&)>& on_output) const {
    return exchangeSocket(socket_path_, payload, MAX_REQUEST_BYTES, MAX_RESPONSE_BYTES, {}, on_output);
}

nlohmann::json RuntimeKernelClient::requestWithTools(
    const nlohmann::json& payload,
    const std::function<nlohmann::json(const std::string&, const nlohmann::json&)>& on_tool,
    const std::function<void(const std::string&)>& on_output) const {
    return exchangeSocket(socket_path_, payload, MAX_TOOL_FRAME_BYTES, MAX_TOOL_FRAME_BYTES, on_tool, on_output);
}
