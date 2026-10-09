#include "memory/RedisMemory.hpp"
#include "memory/RedisClient.hpp"
#include <stdexcept>
#include <utility>
RedisMemory::RedisMemory(const std::string& host, int port, const std::string& user, const std::string& password)
    : RedisMemory(std::make_unique<RedisClient>(host, port, user, password)) {}
RedisMemory::RedisMemory(std::unique_ptr<RedisCommandExecutor> executor) : executor_(std::move(executor)) {
    if (!executor_) throw std::invalid_argument("RedisMemory executor must not be null");
}
RedisMemory::~RedisMemory() = default;
bool RedisMemory::connect() { return executor_->connect(); }
void RedisMemory::disconnect() { executor_->disconnect(); }
std::optional<std::string> RedisMemory::execute(const std::vector<std::string>& command) { return executor_->execute(command); }
