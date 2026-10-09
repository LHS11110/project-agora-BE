#include "memory/MssqlMemory.hpp"
#include "memory/RedisClient.hpp"
#include "memory/ElasticsearchHttpClient.hpp"
#include "Environment.hpp"
#include <iostream>

int main() {
    MssqlMemory sql(environmentValue("DB_HOST"), 1433, environmentValue("DB_USER"),
                    environmentValue("DB_PASSWORD"), environmentValue("DB_NAME"));
    const auto rows = sql.query(SqlCommand("SELECT 1 AS broker_probe;"));
    if (!rows || rows->result_sets.empty() || rows->result_sets.front().rows.empty()) { std::cerr << "SQL through Wall failed\n"; return 1; }
    RedisClient redis;
    if (!redis.connect() || !redis.ping()) { std::cerr << "Redis/Sentinel through Wall failed\n"; return 1; }
    auto es = makeElasticsearchHttpClient(environmentValue("ES_HOST"), 9200);
    if (!es) { std::cerr << "Elasticsearch TLS client could not initialize\n"; return 1; }
    es->set_basic_auth(environmentValue("ES_USER_NAME"), environmentValue("ES_USER_PASSWORD"));
    es->set_connection_timeout(3, 0);
    es->set_read_timeout(5, 0);
    const auto reply = es->Get(("/" + environmentValue("ES_INDEX") + "/_count").c_str());
    if (!reply || reply->status != 200) { std::cerr << "Elasticsearch through Wall failed\n"; return 1; }
    std::cout << "C++ verified SQL, Redis/Sentinel and Elasticsearch through Wall\n";
}
