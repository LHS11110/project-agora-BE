#pragma once

#include <optional>
#include <string>
#include <vector>

// One complete command/reply exchange. nullopt means a transport failure;
// an empty string remains a valid Redis nil/empty reply.
class RedisCommandExecutor {
public:
    virtual ~RedisCommandExecutor() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual std::optional<std::string> execute(const std::vector<std::string>& command) = 0;
};
