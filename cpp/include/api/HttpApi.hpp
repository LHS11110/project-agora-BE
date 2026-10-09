#pragma once
#include <httplib.h>
#include <charconv>
#include <string>
#include <utility>

// Library adapter: routes and request/response tools, never endpoint policy.
class HttpApi final {
public:
    using Request = httplib::Request;
    using Response = httplib::Response;
    using Handler = httplib::Server::Handler;
    explicit HttpApi(httplib::Server& server) : server_(server) {}
    void get(const std::string& path, Handler handler) { server_.Get(path, std::move(handler)); }
    void post(const std::string& path, Handler handler) { server_.Post(path, std::move(handler)); }
    void remove(const std::string& path, Handler handler) { server_.Delete(path, std::move(handler)); }
    static bool positiveId(const std::string& text, int& result) {
        const auto end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, result);
        return parsed.ec == std::errc{} && parsed.ptr == end && result > 0;
    }
private:
    httplib::Server& server_;
};
