#pragma once

#include <optional>
#include <string>

// New C++-originated canvas passwords use PBKDF2-HMAC-SHA256. Spring accepts
// this format as well as BCrypt hashes created by the REST API.
bool isCanvasPasswordHash(const std::string& value);
std::optional<std::string> hashCanvasPassword(const std::string& password);
std::optional<std::string> normalizeCanvasPassword(const std::string& stored);
