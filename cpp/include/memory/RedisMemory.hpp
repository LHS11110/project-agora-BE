#pragma once
#include "memory/RedisCommandExecutor.hpp"
#include <memory>

// Generic fast remote storage. No canvas IDs, JSON paths or cache policy.
class RedisMemory final : public RedisCommandExecutor {
public:
    RedisMemory(const std::string& host = "127.0.0.1", int port = 6379,
                const std::string& user = {}, const std::string& password = {});
    explicit RedisMemory(std::unique_ptr<RedisCommandExecutor> executor);
    ~RedisMemory() override;
    bool connect() override;
    void disconnect() override;
    std::optional<std::string> execute(const std::vector<std::string>& command) override;
private:
    std::unique_ptr<RedisCommandExecutor> executor_;
};
