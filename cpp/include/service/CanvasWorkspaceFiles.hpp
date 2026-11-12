#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace agora::workspace {
using Json = nlohmann::json;

inline bool path(const Json& value) {
    if (!value.is_string()) return false;
    const auto text = value.get<std::string>();
    if (text.empty() || text[0] != '/' || text.size() > 4096 || text.find('\\') != std::string::npos || text.find('\0') != std::string::npos) return false;
    if (text == "/") return true;
    std::size_t start = 1;
    while (start < text.size()) {
        const auto end = text.find('/', start);
        const auto part = text.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.size() > 255) return false;
        if (end == std::string::npos) return true;
        start = end + 1;
    }
    return false;
}

inline bool readOnly(const std::string& operation) {
    return operation == "read" || operation == "list" || operation == "stat";
}

inline void validate(const Json& event) {
    static const std::unordered_set<std::string> operations = {"read", "list", "stat", "write", "mkdir", "rename", "delete"};
    static const std::unordered_set<std::string> fields = {"type", "request_id", "operation", "path", "document", "content", "fileId", "etag", "resourceId", "create", "overwrite", "recursive", "target", "cursor"};
    if (!event.is_object() || event.dump().size() > 700000 || !event.contains("operation") || !event["operation"].is_string()
        || !operations.count(event["operation"].get<std::string>()) || !event.contains("path") || !path(event["path"]))
        throw std::invalid_argument("WORKSPACE_REQUEST_INVALID");
    for (const auto& field : event.items()) if (!fields.count(field.key())) throw std::invalid_argument("WORKSPACE_REQUEST_INVALID");
    if (!event.contains("request_id") || !event["request_id"].is_string() || event["request_id"].get_ref<const std::string&>().empty()
        || event["request_id"].get_ref<const std::string&>().size() > 128) throw std::invalid_argument("WORKSPACE_REQUEST_INVALID");
    if (event["operation"] == "rename" && (!event.contains("target") || !path(event["target"]))) throw std::invalid_argument("WORKSPACE_REQUEST_INVALID");
    for (const char* key : {"document", "content", "fileId", "etag", "resourceId"})
        if (event.contains(key) && !event[key].is_string()) throw std::invalid_argument("WORKSPACE_REQUEST_INVALID");
    for (const char* key : {"create", "overwrite", "recursive"})
        if (event.contains(key) && !event[key].is_boolean()) throw std::invalid_argument("WORKSPACE_REQUEST_INVALID");
    if (event.contains("cursor") && (!event["cursor"].is_number_integer() || event["cursor"].get<long long>() < 0)) throw std::invalid_argument("WORKSPACE_REQUEST_INVALID");
}
}
