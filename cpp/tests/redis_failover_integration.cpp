#include "CanvasMemory.hpp"
#include "TestSupport.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
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

std::optional<std::vector<std::string>> parseQuotedJsonPath(const std::string& path) {
    if (path.empty() || path[0] != '$') return std::nullopt;
    std::vector<std::string> segments;
    std::size_t position = 1;
    while (position < path.size()) {
        if (path[position] != '[' || position + 1 >= path.size() || path[position + 1] != '"') {
            return std::nullopt;
        }
        const std::size_t begin = position + 1;
        position = begin + 1;
        bool escaped = false;
        bool closed = false;
        for (; position < path.size(); ++position) {
            const char character = path[position];
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') {
                closed = true;
                break;
            }
        }
        const std::size_t end = position + 1;
        if (!closed || end >= path.size() || path[end] != ']') return std::nullopt;
        try {
            const auto segment = nlohmann::json::parse(path.substr(begin, end - begin));
            if (!segment.is_string()) return std::nullopt;
            segments.push_back(segment.get<std::string>());
        } catch (...) {
            return std::nullopt;
        }
        position = end + 1;
    }
    return segments;
}

const nlohmann::json* findFakeJsonPath(
        const nlohmann::json& document, const std::vector<std::string>& segments) {
    const nlohmann::json* current = &document;
    for (const auto& segment : segments) {
        if (!current->is_object() || !current->contains(segment)) return nullptr;
        current = &(*current)[segment];
    }
    return current;
}

bool setFakeJsonPath(nlohmann::json& document, const std::vector<std::string>& segments,
                     const nlohmann::json& value) {
    if (segments.empty()) {
        document = value;
        return true;
    }
    nlohmann::json* current = &document;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        if (!current->is_object() || !current->contains(segments[i])) return false;
        current = &(*current)[segments[i]];
    }
    if (!current->is_object()) return false;
    (*current)[segments.back()] = value;
    return true;
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
    void rejectJsonDeleteWithNoAuth() { reject_json_delete_with_noauth_ = true; }
    int pingCount() const { return ping_count_; }
    int jsonGetCount(const std::string& key, const std::string& path) const {
        std::lock_guard<std::mutex> lock(document_mutex_);
        const auto found = json_get_counts_.find(key + "\n" + path);
        return found == json_get_counts_.end() ? 0 : found->second;
    }
    int jsonGetTotalCount(const std::string& key) const {
        std::lock_guard<std::mutex> lock(document_mutex_);
        const std::string prefix = key + "\n";
        int total = 0;
        for (const auto& [counted_path, count] : json_get_counts_) {
            if (counted_path.compare(0, prefix.size(), prefix) == 0) total += count;
        }
        return total;
    }
    std::optional<nlohmann::json> document(const std::string& key) const {
        std::lock_guard<std::mutex> lock(document_mutex_);
        const auto found = documents_.find(key);
        if (found == documents_.end()) return std::nullopt;
        return found->second;
    }

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
            } else if (name == "JSON.GET" && command.size() >= 2) {
                const std::string path = command.size() >= 3 ? command[2] : "$";
                std::string response = "$-1\r\n";
                {
                    std::lock_guard<std::mutex> lock(document_mutex_);
                    ++json_get_counts_[command[1] + "\n" + path];
                    const auto document = documents_.find(command[1]);
                    const auto segments = parseQuotedJsonPath(path);
                    if (document != documents_.end() && segments) {
                        if (const auto* value = findFakeJsonPath(document->second, *segments)) {
                            response = bulkString(segments->empty()
                                ? value->dump() : nlohmann::json::array({*value}).dump());
                        } else {
                            response = bulkString("[]");
                        }
                    }
                }
                writeAll(socket_fd, response);
            } else if (name == "JSON.SET" && command.size() == 4) {
                const auto segments = parseQuotedJsonPath(command[2]);
                try {
                    const auto value = nlohmann::json::parse(command[3]);
                    std::lock_guard<std::mutex> lock(document_mutex_);
                    if (segments && setFakeJsonPath(documents_[command[1]], *segments, value)) {
                        writeAll(socket_fd, "+OK\r\n");
                    } else {
                        writeAll(socket_fd, "-ERR unsupported test JSON path\r\n");
                    }
                } catch (...) {
                    writeAll(socket_fd, "-ERR invalid test JSON\r\n");
                }
            } else if (name == "JSON.DEL" && command.size() == 3) {
                if (reject_json_delete_with_noauth_) {
                    writeAll(socket_fd, "-NOAUTH Authentication required.\r\n");
                    continue;
                }
                const auto segments = parseQuotedJsonPath(command[2]);
                int removed = 0;
                {
                    std::lock_guard<std::mutex> lock(document_mutex_);
                    auto document = documents_.find(command[1]);
                    if (document != documents_.end() && segments && !segments->empty()) {
                        nlohmann::json* parent = &document->second;
                        for (std::size_t i = 0; i + 1 < segments->size(); ++i) {
                            if (!parent->is_object() || !parent->contains((*segments)[i])) {
                                parent = nullptr;
                                break;
                            }
                            parent = &(*parent)[(*segments)[i]];
                        }
                        if (parent && parent->is_object()) {
                            removed = static_cast<int>(parent->erase(segments->back()));
                        }
                    }
                }
                writeAll(socket_fd, ":" + std::to_string(removed) + "\r\n");
            } else {
                writeAll(socket_fd, "-ERR unsupported test command\r\n");
            }
        }
    }

    std::atomic<bool> read_only_{false};
    std::atomic<bool> reject_json_delete_with_noauth_{false};
    std::atomic<int> ping_count_{0};
    mutable std::mutex document_mutex_;
    std::unordered_map<std::string, nlohmann::json> documents_;
    std::unordered_map<std::string, int> json_get_counts_;
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
    ::setenv("REDIS_TLS_ENABLED", "false", 1);
    ::unsetenv("REDIS_TLS_CA_CERT");
    ::setenv("ES_LOG_USER_PASSWORD", "", 1);
    ::setenv("CPP_CANVAS_LRU_CAPACITY", "2", 1);
    ::setenv("CPP_CANVAS_LRU_ITEMS_PER_CANVAS", "1", 1);

    {
        CanvasMemory client("127.0.0.1", primary_a.port(), "", "cpp-test-password");
        AGORA_CHECK(client.connect());
        AGORA_CHECK(client.ping());

        const std::string canvas_a = "canvas:987654320";
        const std::string hot_path = "$[\"items\"][\"hot\"][\"value\"]";
        const std::string cold_path = "$[\"items\"][\"cold\"][\"value\"]";
        const std::string rare_path = "$[\"items\"][\"rare\"][\"value\"]";
        AGORA_CHECK(client.set(canvas_a, nlohmann::json{
            {"items", {{"hot", {{"value", 1}}}, {"cold", {{"value", 10}}},
                       {"rare", {{"value", 99}}}}}
        }.dump()));

        const std::string created_item_path = "$[\"items\"][\"created\"]";
        const std::string created_value_path = "$[\"items\"][\"created\"][\"value\"]";
        AGORA_CHECK(client.setJsonPath(canvas_a, created_item_path, nlohmann::json{{"value", 7}}));
        const int created_reads_before_lookup = primary_a.jsonGetCount(canvas_a, created_value_path);
        AGORA_CHECK(client.getJsonPath(canvas_a, created_value_path) == std::string("[7]"));
        AGORA_CHECK(primary_a.jsonGetCount(canvas_a, created_value_path) == created_reads_before_lookup + 1);
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("created").at("value") == 7);

        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[1]"));
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[1]"));
        const int hot_reads_before_hit = primary_a.jsonGetCount(canvas_a, hot_path);
        const int canvas_reads_before_hit = primary_a.jsonGetTotalCount(canvas_a);
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[1]"));
        AGORA_CHECK(primary_a.jsonGetCount(canvas_a, hot_path) == hot_reads_before_hit);
        AGORA_CHECK(primary_a.jsonGetTotalCount(canvas_a) == canvas_reads_before_hit);

        // A one-off item read uses only its requested Redis path and is not promoted.
        const int rare_path_reads_before = primary_a.jsonGetCount(canvas_a, rare_path);
        const int canvas_reads_before_rare = primary_a.jsonGetTotalCount(canvas_a);
        AGORA_CHECK(client.getJsonPath(canvas_a, rare_path) == std::string("[99]"));
        AGORA_CHECK(primary_a.jsonGetCount(canvas_a, rare_path) == rare_path_reads_before + 1);
        AGORA_CHECK(primary_a.jsonGetTotalCount(canvas_a) == canvas_reads_before_rare + 1);

        // Cached edits stay in the LRU until eviction or an explicit sync.
        AGORA_CHECK(client.setJsonPath(canvas_a, hot_path, 2));
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("hot").at("value") == 1);
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[2]"));
        const auto merged_document = client.get(canvas_a);
        AGORA_CHECK(merged_document.has_value());
        AGORA_CHECK(nlohmann::json::parse(*merged_document).at("items").at("hot").at("value") == 2);
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("hot").at("value") == 1);
        AGORA_CHECK(client.getJsonPath(canvas_a, cold_path) == std::string("[10]"));
        AGORA_CHECK(client.getJsonPath(canvas_a, cold_path) == std::string("[10]"));
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("hot").at("value") == 2);
        AGORA_CHECK(client.getJsonPath(canvas_a, cold_path) == std::string("[10]"));
        const int hot_reads_before_reload = primary_a.jsonGetCount(canvas_a, hot_path);
        const int canvas_reads_before_reload = primary_a.jsonGetTotalCount(canvas_a);
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[2]"));
        AGORA_CHECK(primary_a.jsonGetCount(canvas_a, hot_path) == hot_reads_before_reload + 1);
        AGORA_CHECK(primary_a.jsonGetTotalCount(canvas_a) == canvas_reads_before_reload + 1);

        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[2]"));
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[2]"));
        AGORA_CHECK(client.setJsonPath(canvas_a, hot_path, 3));
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("hot").at("value") == 2);
        AGORA_CHECK(client.flushCanvasItemCache(canvas_a));
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("hot").at("value") == 3);

        // Each canvas has its own item capacity and does not evict another canvas's cache.
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[3]"));
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[3]"));
        const std::string canvas_b = "canvas:987654321";
        const std::string canvas_b_path = "$[\"items\"][\"hot\"][\"value\"]";
        AGORA_CHECK(client.set(canvas_b, nlohmann::json{
            {"items", {{"hot", {{"value", 3}}}}}
        }.dump()));
        AGORA_CHECK(client.getJsonPath(canvas_b, canvas_b_path) == std::string("[3]"));
        AGORA_CHECK(client.getJsonPath(canvas_b, canvas_b_path) == std::string("[3]"));
        AGORA_CHECK(client.getJsonPath(canvas_b, canvas_b_path) == std::string("[3]"));
        const int canvas_a_reads = primary_a.jsonGetCount(canvas_a, hot_path);
        const int canvas_b_reads = primary_a.jsonGetCount(canvas_b, canvas_b_path);
        const int canvas_a_total_reads = primary_a.jsonGetTotalCount(canvas_a);
        const int canvas_b_total_reads = primary_a.jsonGetTotalCount(canvas_b);
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[3]"));
        AGORA_CHECK(client.getJsonPath(canvas_b, canvas_b_path) == std::string("[3]"));
        AGORA_CHECK(primary_a.jsonGetCount(canvas_a, hot_path) == canvas_a_reads);
        AGORA_CHECK(primary_a.jsonGetCount(canvas_b, canvas_b_path) == canvas_b_reads);
        AGORA_CHECK(primary_a.jsonGetTotalCount(canvas_a) == canvas_a_total_reads);
        AGORA_CHECK(primary_a.jsonGetTotalCount(canvas_b) == canvas_b_total_reads);

        AGORA_CHECK(client.setJsonPath(canvas_a, hot_path, 4));
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("hot").at("value") == 3);
        AGORA_CHECK(client.getJsonPath(canvas_b, canvas_b_path) == std::string("[3]"));
        std::string canvas_c;
        for (int candidate = 987654322; candidate < 987654500; ++candidate) {
            const std::string key = "canvas:" + std::to_string(candidate);
            if (std::hash<std::string>{}(key) % 64
                    != std::hash<std::string>{}(canvas_a) % 64) {
                canvas_c = key;
                break;
            }
        }
        AGORA_CHECK(!canvas_c.empty());
        const std::string canvas_c_path = "$[\"items\"][\"warm\"][\"value\"]";
        AGORA_CHECK(client.set(canvas_c, nlohmann::json{
            {"items", {{"warm", {{"value", 12}}}}}
        }.dump()));
        AGORA_CHECK(client.getJsonPath(canvas_c, canvas_c_path) == std::string("[12]"));
        AGORA_CHECK(primary_a.document(canvas_a)->at("items").at("hot").at("value") == 4);

        AGORA_CHECK(client.deleteJsonPath(canvas_a, "$[\"items\"][\"hot\"]"));
        AGORA_CHECK(!primary_a.document(canvas_a)->at("items").contains("hot"));
        AGORA_CHECK(client.getJsonPath(canvas_a, hot_path) == std::string("[]"));

        // The old primary remains reachable but has become a replica.
        primary_a.becomeReadOnly();
        AGORA_CHECK(!client.ping());

        // Sentinel promotes B. The next command reconnects and resolves it.
        sentinel.promote(primary_b.port());
        AGORA_CHECK(client.ping());
        AGORA_CHECK(primary_b.pingCount() == 1);
        AGORA_CHECK(sentinel.authenticatedQueryCount() > 0);

        // RESP error replies must not be mistaken for successful JSON deletes.
        primary_b.rejectJsonDeleteWithNoAuth();
        AGORA_CHECK(!client.deleteJsonPath("canvas:987654321", "$.items"));
    }
}

void runRedisTlsSentinelSmoke() {
    const char* configured_seeds = std::getenv("REDIS_SENTINELS");
    AGORA_CHECK(configured_seeds != nullptr && *configured_seeds != '\0');
    AGORA_CHECK(std::getenv("REDIS_TLS_ENABLED") != nullptr);
    AGORA_CHECK(std::string(std::getenv("REDIS_TLS_ENABLED")) == "true");

    std::istringstream input(configured_seeds);
    std::string seed;
    int checked = 0;
    while (std::getline(input, seed, ',')) {
        if (seed.empty()) continue;
        ::setenv("REDIS_SENTINELS", seed.c_str(), 1);
        CanvasMemory client;
        AGORA_CHECK(client.connect());
        AGORA_CHECK(client.ping());
        client.disconnect();
        ++checked;
    }
    AGORA_CHECK(checked == 3);
}
}

int main() {
    if (std::getenv("AGORA_REDIS_TLS_SMOKE")) {
        return runTest("Redis TLS Sentinel discovery and primary verification", runRedisTlsSentinelSmoke);
    }
    return runTest("Redis Sentinel primary promotion and client reconnection", runRedisSentinelFailover);
}
