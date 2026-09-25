#pragma once

#include <httplib.h>

#include <cstdlib>
#include <memory>
#include <string>

inline std::unique_ptr<httplib::Client> makeElasticsearchHttpClient(
        const std::string& host, int port) {
    const char* configured_scheme = std::getenv("ES_SCHEME");
    const std::string scheme = configured_scheme && *configured_scheme
        ? configured_scheme : "http";
    if (scheme != "http" && scheme != "https") return {};

    std::string url_host = host;
    if (url_host.find(':') != std::string::npos
            && (url_host.empty() || url_host.front() != '[')) {
        url_host = "[" + url_host + "]";
    }
    auto client = std::make_unique<httplib::Client>(
            scheme + "://" + url_host + ":" + std::to_string(port));
    if (!client->is_valid()) return {};

    if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        const char* ca_certificate = std::getenv("ES_CA_CERT");
        if (ca_certificate && *ca_certificate) client->set_ca_cert_path(ca_certificate);
        client->enable_server_certificate_verification(true);
#else
        return {};
#endif
    }
    return client;
}
