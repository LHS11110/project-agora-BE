#include "api/BrokerSocketApi.hpp"
#include "Environment.hpp"
#include <openssl/ssl.h>
#include <stdexcept>
std::unique_ptr<BrokerSocketApi::App> BrokerSocketApi::create() {
    const auto certificate = environmentValue("SERVICE_TLS_CERT");
    const auto key = environmentValue("SERVICE_TLS_KEY");
    uWS::SocketContextOptions options;
    options.cert_file_name = certificate.c_str(); options.key_file_name = key.c_str();
    auto app = std::make_unique<App>(options);
    auto* context = static_cast<SSL_CTX*>(app->getNativeHandle());
    if (!context || SSL_CTX_set_min_proto_version(context, TLS1_3_VERSION) != 1)
        throw std::runtime_error("Cannot enforce TLS 1.3 for broker socket");
    return app;
}
