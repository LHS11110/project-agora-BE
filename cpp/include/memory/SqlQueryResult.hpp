#pragma once

#include <optional>
#include <string>
#include <vector>

/** SQL values are returned as text so domain repositories own type mapping. */
struct SqlRow {
    std::vector<std::optional<std::string>> values;
};

struct SqlResultSet {
    std::vector<std::string> columns;
    std::vector<SqlRow> rows;
};

struct SqlQueryResult {
    std::vector<SqlResultSet> result_sets;
};
