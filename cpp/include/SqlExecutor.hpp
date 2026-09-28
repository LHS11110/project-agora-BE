#pragma once

#include "SqlCommand.hpp"
#include "SqlQueryResult.hpp"

#include <optional>

/** Port used by feature repositories to execute parameterized relational queries. */
class SqlExecutor {
public:
    virtual ~SqlExecutor() = default;
    virtual std::optional<SqlQueryResult> query(const SqlCommand& command) = 0;
    virtual bool execute(const SqlCommand& command) = 0;
};
