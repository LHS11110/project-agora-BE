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
}

nlohmann::json RuntimeKernelClient::request(const nlohmann::json& payload,
                                             const std::function<void(const std::string&)>& on_output) const {
    sockaddr_un address{};
    if (socket_path_.empty() || socket_path_.size() >= sizeof(address.sun_path))
        throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
    const auto closeSocket = [&] { ::shutdown(fd, SHUT_RDWR); ::close(fd); };

    timeval timeout{240, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeval send_timeout{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket();
        throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
    }

    const std::string request = payload.dump() + "\n";
    if (request.size() > MAX_REQUEST_BYTES) {
        closeSocket();
        throw std::runtime_error("RUNTIME_REQUEST_TOO_LARGE");
    }
    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto count = ::send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) {
            closeSocket();
            throw std::runtime_error("RUNTIME_MANAGER_UNAVAILABLE");
        }
        sent += static_cast<std::size_t>(count);
    }

    std::string response;
    char buffer[8192];
    while (true) {
        const auto newline = response.find('\n');
        if (newline != std::string::npos) {
            const auto line = response.substr(0, newline);
            response.erase(0, newline + 1);
            try {
                const auto frame = nlohmann::json::parse(line);
                if (frame.is_object() && frame.value("type", "") == "output") {
                    if (on_output && frame.contains("output") && frame["output"].is_string())
                        on_output(frame["output"].get_ref<const std::string&>());
                    continue;
                }
                closeSocket();
                return frame;
            } catch (const std::runtime_error&) {
                closeSocket();
                throw;
            } catch (...) {
                closeSocket();
                throw std::runtime_error("RUNTIME_VM_UNAVAILABLE");
            }
        }
        if (response.size() >= MAX_RESPONSE_BYTES) {
            closeSocket();
            throw std::runtime_error("RUNTIME_RESPONSE_TOO_LARGE");
        }
        const auto count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            closeSocket();
            throw std::runtime_error("RUNTIME_VM_UNAVAILABLE");
        }
        response.append(buffer, static_cast<std::size_t>(count));
    }
}
