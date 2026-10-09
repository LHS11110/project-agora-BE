#include "memory/ElasticsearchMemory.hpp"
#include "memory/ElasticsearchHttpClient.hpp"
#include "Environment.hpp"
#include <memory>
namespace {
std::string envOr(const char* name, const std::string& value) {
    return value.empty() ? environmentValue(name) : value;
}
std::string encodePathSegment(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) {
        const bool unreserved = (byte >= 'a' && byte <= 'z')
                || (byte >= 'A' && byte <= 'Z')
                || (byte >= '0' && byte <= '9')
                || byte == '-' || byte == '_' || byte == '.' || byte == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(byte));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[byte >> 4]);
            encoded.push_back(hex[byte & 0x0f]);
        }
    }
    return encoded;
}

std::unique_ptr<httplib::Client> makeAuthenticatedClient(
        const std::string& host, int port,
        const std::string& user, const std::string& password) {
    if (user.empty() || password.empty()) return {};
    auto client = makeElasticsearchHttpClient(host, port);
    if (!client) return {};
    client->set_connection_timeout(3, 0);
    client->set_read_timeout(3, 0);
    client->set_write_timeout(3, 0);
    client->set_basic_auth(user, password);
    return client;
}
}

ElasticsearchMemory::ElasticsearchMemory(const std::string& host, int port,
                   const std::string& user,
                   const std::string& pass)
    : host_(host), port_(port), user_(envOr("ES_USER_NAME", user)),
      pass_(envOr("ES_USER_PASSWORD", pass)) {
}

std::optional<nlohmann::json> ElasticsearchMemory::getDocument(
        const std::string& index, const std::string& document_id) {
    if (index.empty() || document_id.empty()) return std::nullopt;
    auto client = makeAuthenticatedClient(host_, port_, user_, pass_);
    if (!client) return std::nullopt;

    const std::string path = "/" + encodePathSegment(index) + "/_doc/" + encodePathSegment(document_id);
    const auto response = client->Get(path);
    if (!response || response->status != 200) return std::nullopt;
    try {
        const auto body = nlohmann::json::parse(response->body);
        if (!body.contains("_source")) return std::nullopt;
        return body["_source"];
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool ElasticsearchMemory::putDocument(const std::string& index, const std::string& document_id,
                           const nlohmann::json& document) {
    if (index.empty() || document_id.empty() || !document.is_object()) return false;
    auto client = makeAuthenticatedClient(host_, port_, user_, pass_);
    if (!client) return false;

    const std::string path = "/" + encodePathSegment(index) + "/_doc/" + encodePathSegment(document_id);
    const auto response = client->Put(path, document.dump(), "application/json");
    return response && response->status >= 200 && response->status < 300;
}

bool ElasticsearchMemory::patchDocument(const std::string& index, const std::string& document_id,
                             const nlohmann::json& fields) {
    if (index.empty() || document_id.empty() || !fields.is_object() || fields.empty()) return false;
    auto client = makeAuthenticatedClient(host_, port_, user_, pass_);
    if (!client) return false;

    const std::string path = "/" + encodePathSegment(index) + "/_update/" + encodePathSegment(document_id) + "?retry_on_conflict=3&refresh=true";
    const nlohmann::json body = {{"doc", fields}};
    const auto response = client->Post(path, body.dump(), "application/json");
    return response && response->status >= 200 && response->status < 300;
}

bool ElasticsearchMemory::deleteDocument(const std::string& index, const std::string& document_id) {
    if (index.empty() || document_id.empty()) return false;
    auto client = makeAuthenticatedClient(host_, port_, user_, pass_);
    if (!client) return false;

    const std::string path = "/" + encodePathSegment(index) + "/_doc/" + encodePathSegment(document_id);
    const auto response = client->Delete(path);
    return response && response->status >= 200 && response->status < 300;
}

std::optional<nlohmann::json> ElasticsearchMemory::searchDocuments(
        const std::string& index, const nlohmann::json& query) {
    if (index.empty() || !query.is_object()) return std::nullopt;
    auto client = makeAuthenticatedClient(host_, port_, user_, pass_);
    if (!client) return std::nullopt;

    const std::string path = "/" + encodePathSegment(index) + "/_search";
    const auto response = client->Post(path, query.dump(), "application/json");
    if (!response || response->status < 200 || response->status >= 300) return std::nullopt;
    try {
        return nlohmann::json::parse(response->body);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

