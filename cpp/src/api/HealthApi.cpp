#include "HealthApi.hpp"

void HealthApi::registerRoutes(httplib::Server& server) {
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(R"({"status":"UP","service":"Agora C++ Realtime Server"})", "application/json");
    });

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(R"({"status":"online","service":"Agora C++ Server"})", "application/json");
    });
}
