#include "memory/MssqlMemory.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    const char* host = std::getenv("DB_HOST");
    if (!host || !std::getenv("AGORA_TEST_MSSQL_RECOVERY")) return 77;
    MssqlMemory sql(host);
    // A client-side timeout must fail this request, never terminate the server.
    if (sql.query(SqlCommand("WAITFOR DELAY '00:00:08'; SELECT 1 AS value;"))) return 1;
    // The failed pooled session must be discarded before the next request.
    auto recovered = sql.query(SqlCommand("SELECT 42 AS value;"));
    if (!recovered || recovered->result_sets.empty()
        || recovered->result_sets[0].rows.empty()
        || recovered->result_sets[0].rows[0].values[0] != "42") return 2;
    std::cout << "SQL timeout survived; next query recovered\n";
}
