#pragma once
#include "memory/SqlExecutor.hpp"
#include <string>
class MssqlMemory final : public SqlExecutor {
public:
    MssqlMemory(const std::string& host = "127.0.0.1", int port = 1433,
                const std::string& user = {}, const std::string& password = {}, const std::string& database = {});
    ~MssqlMemory() override;
    std::optional<SqlQueryResult> query(const SqlCommand& command) override;
    bool execute(const SqlCommand& command) override;
private:
    std::string host_; int port_; std::string user_, pass_, db_;
};
