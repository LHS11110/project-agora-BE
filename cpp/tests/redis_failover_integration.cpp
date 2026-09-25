#include "RedisClient.hpp"
#include "TestSupport.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
bool readLine(int socket_fd, std::string& line) {
    line.clear();
    char previous = '\0';
    while (true) {
        char current = '\0';
        const ssize_t count = ::read(socket_fd, &current, 1);
        if (count != 1) return false;
        if (previous == '\r' && current == '\n') {
            line.pop_back();
            return true;
        }
        line.push_back(current);
        previous = current;
    }
}

bool readCommand(int socket_fd, std::vector<std::string>& parts) {
    char marker = '\0';
    if (::read(socket_fd, &marker, 1) != 1) return false;
    if (marker != '*') return false;

    std::string line;
    if (!readLine(socket_fd, line)) return false;
    const int count = std::stoi(line);
    parts.clear();
    parts.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (::read(socket_fd, &marker, 1) != 1 || marker != '$') return false;
        if (!readLine(socket_fd, line)) return false;
        const std::size_t length = static_cast<std::size_t>(std::stoul(line));
        std::string value(length, '\0');
        std::size_t offset = 0;
        while (offset < length) {
            const ssize_t received = ::read(socket_fd, value.data() + offset, length - offset);
            if (received <= 0) return false;
            offset += static_cast<std::size_t>(received);
        }
        char crlf[2]{};
        if (::read(socket_fd, crlf, sizeof(crlf)) != static_cast<ssize_t>(sizeof(crlf))
                || crlf[0] != '\r' || crlf[1] != '\n') return false;
        parts.push_back(std::move(value));
    }
    return true;
}

void writeAll(int socket_fd, const std::string& value) {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const ssize_t sent = ::write(socket_fd, value.data() + offset, value.size() - offset);
        if (sent <= 0) return;
        offset += static_cast<std::size_t>(sent);
    }
}

std::string bulkString(const std::string& value) {
    return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
}

class FakeTcpServer {
public:
    FakeTcpServer() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) throw std::runtime_error("socket() failed");

        int reuse = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0
                || ::listen(listener_, 16) < 0) {
            ::close(listener_);
            throw std::runtime_error("could not bind loopback test server");
        }
        socklen_t address_size = sizeof(address);
        if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &address_size) < 0) {
            ::close(listener_);
            throw std::runtime_error("getsockname() failed");
        }
        port_ = ntohs(address.sin_port);
    }

    virtual ~FakeTcpServer() { stop(); }

    int port() const { return port_; }

    void start() {
        if (running_.exchange(true)) return;
        accept_thread_ = std::thread([this]() {
            while (running_) {
                const int client = ::accept(listener_, nullptr, nullptr);
                if (client < 0) {
                    if (!running_) break;
                    if (errno == EINTR) continue;
                    continue;
                }
                timeval timeout{2, 0};
                ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                std::lock_guard<std::mutex> lock(clients_mutex_);
                client_fds_.push_back(client);
                client_threads_.emplace_back([this, client]() {
                    handleClient(client);
                    ::shutdown(client, SHUT_RDWR);
                    ::close(client);
                    std::lock_guard<std::mutex> client_lock(clients_mutex_);
                    for (auto it = client_fds_.begin(); it != client_fds_.end(); ++it) {
                        if (*it == client) {
                            client_fds_.erase(it);
                            break;
                        }
                    }
                });
            }
        });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        if (accept_thread_.joinable()) accept_thread_.join();
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (int client : client_fds_) ::shutdown(client, SHUT_RDWR);
        }
        for (auto& client_thread : client_threads_) {
            if (client_thread.joinable()) client_thread.join();
        }
    }

protected:
    virtual void handleClient(int socket_fd) = 0;

private:
    int listener_{-1};
    int port_{0};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::vector<int> client_fds_;
    std::vector<std::thread> client_threads_;
};

class FakeRedisNode final : public FakeTcpServer {
public:
    ~FakeRedisNode() override { stop(); }
    void becomeReadOnly() { read_only_ = true; }
    int pingCount() const { return ping_count_; }

private:
    void handleClient(int socket_fd) override {
        std::vector<std::string> command;
        while (readCommand(socket_fd, command)) {
            if (command.empty()) return;
            const std::string& name = command.front();
            if (name == "AUTH") {
                writeAll(socket_fd, "+OK\r\n");
            } else if (name == "ROLE") {
                const std::string role = read_only_ ? "slave" : "master";
                writeAll(socket_fd, "*3\r\n" + bulkString(role) + ":0\r\n*0\r\n");
            } else if (name == "PING") {
                ++ping_count_;
                if (read_only_) writeAll(socket_fd, "-READONLY You can't write against a read only replica.\r\n");
                else writeAll(socket_fd, "+PONG\r\n");
            } else {
                writeAll(socket_fd, "-ERR unsupported test command\r\n");
            }
        }
    }

    std::atomic<bool> read_only_{false};
    std::atomic<int> ping_count_{0};
};

class FakeSentinel final : public FakeTcpServer {
public:
    FakeSentinel(int initial_master_port, std::string username, std::string password)
        : master_port_(initial_master_port), username_(std::move(username)),
          password_(std::move(password)) {}
    ~FakeSentinel() override { stop(); }
    void promote(int port) { master_port_ = port; }
    int authenticatedQueryCount() const { return authenticated_query_count_; }

private:
    void handleClient(int socket_fd) override {
        std::vector<std::string> command;
        if (!readCommand(socket_fd, command)
                || command.size() != 3
                || command[0] != "AUTH"
                || command[1] != username_
                || command[2] != password_) {
            writeAll(socket_fd, "-ERR Sentinel authentication required\r\n");
            return;
        }
        writeAll(socket_fd, "+OK\r\n");
        ++authenticated_query_count_;
        if (!readCommand(socket_fd, command) || command.size() < 3 || command[0] != "SENTINEL") {
            writeAll(socket_fd, "-ERR unexpected sentinel command\r\n");
            return;
        }
        const std::string response = "*2\r\n" + bulkString("127.0.0.1")
                + bulkString(std::to_string(master_port_));
        writeAll(socket_fd, response);
    }

    std::atomic<int> master_port_;
    std::string username_;
    std::string password_;
    std::atomic<int> authenticated_query_count_{0};
};

void runRedisSentinelFailover() {
    FakeRedisNode primary_a;
    FakeRedisNode primary_b;
    FakeSentinel sentinel(primary_a.port(), "sentinel-reader-test", "sentinel-test-password");
    primary_a.start();
    primary_b.start();
    sentinel.start();

    ::setenv("REDIS_SENTINELS", ("127.0.0.1:" + std::to_string(sentinel.port())).c_str(), 1);
    ::setenv("REDIS_SENTINEL_MASTER_NAME", "agora-master", 1);
    ::setenv("REDIS_SENTINEL_USER", "sentinel-reader-test", 1);
    ::setenv("REDIS_SENTINEL_PASSWORD", "sentinel-test-password", 1);
    ::setenv("REDIS_USER", "", 1);
    ::setenv("REDIS_USER_PASSWORD", "cpp-test-password", 1);
    ::setenv("ES_LOG_USER_PASSWORD", "", 1);

    {
        RedisClient client("127.0.0.1", primary_a.port(), "", "cpp-test-password");
        AGORA_CHECK(client.connect());
        AGORA_CHECK(client.ping());

        // The old primary remains reachable but has become a replica.
        primary_a.becomeReadOnly();
        AGORA_CHECK(!client.ping());

        // Sentinel promotes B. The next command reconnects and resolves it.
        sentinel.promote(primary_b.port());
        AGORA_CHECK(client.ping());
        AGORA_CHECK(primary_b.pingCount() == 1);
        AGORA_CHECK(sentinel.authenticatedQueryCount() > 0);
    }
}
}

int main() {
    return runTest("Redis Sentinel primary promotion and client reconnection", runRedisSentinelFailover);
}
