#include "RedisClient.hpp"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

RedisClient::RedisClient(const std::string& host, int port, const std::string& password)
    : host_(host), port_(port), password_(password), socket_fd_(-1) {
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

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[RedisClient] Connect failed to " << host_ << ":" << port_ << "\n";
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (!password_.empty()) {
        sendCommand({"AUTH", password_});
        std::string auth_res = readResponse();
        if (auth_res.find("ERR") != std::string::npos && auth_res.find("no password is set") == std::string::npos) {
            std::cerr << "[RedisClient] Auth warning: " << auth_res << "\n";
        }
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
    ssize_t sent = write(socket_fd_, msg.data(), msg.length());
    if (sent < (ssize_t)msg.length()) {
        disconnect();
        return false;
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
        read(socket_fd_, crlf, 2);
        return std::string(buf.data(), len);
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
    if (!sendCommand({"SET", key, value})) return false;
    std::string res = readResponse();
    return res == "OK";
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    if (!sendCommand({"GET", key})) return std::nullopt;
    std::string res = readResponse();
    if (res.empty()) return std::nullopt;
    return res;
}

bool RedisClient::del(const std::string& key) {
    if (!sendCommand({"DEL", key})) return false;
    readResponse();
    return true;
}

bool RedisClient::deletePattern(const std::string& pattern) {
    if (!sendCommand({"KEYS", pattern})) return false;
    std::string keys_str = readResponse();
    if (keys_str.empty()) return true;

    std::istringstream iss(keys_str);
    std::string key;
    std::vector<std::string> del_args = {"DEL"};
    while (iss >> key) {
        del_args.push_back(key);
    }
    if (del_args.size() > 1) {
        sendCommand(del_args);
        readResponse();
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
