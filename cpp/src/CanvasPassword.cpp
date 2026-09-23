#include "CanvasPassword.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <cctype>

namespace {
constexpr int kIterations = 310000;

std::string toHex(const unsigned char* bytes, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        output.push_back(digits[bytes[i] >> 4]);
        output.push_back(digits[bytes[i] & 0xf]);
    }
    return output;
}

bool lowercaseHex(const std::string& value) {
    for (char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && (c < 'a' || c > 'f')) return false;
    }
    return true;
}
}

bool isCanvasPasswordHash(const std::string& value) {
    if (value.size() == 60 && value[0] == '$' && value[1] == '2'
        && (value[2] == 'a' || value[2] == 'b' || value[2] == 'y')
        && value[3] == '$' && std::isdigit(static_cast<unsigned char>(value[4]))
        && std::isdigit(static_cast<unsigned char>(value[5])) && value[6] == '$') {
        bool valid = true;
        for (std::size_t i = 7; i < value.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if (!std::isalnum(c) && c != '.' && c != '/') { valid = false; break; }
        }
        if (valid) return true;
    }
    const auto first = value.find('$');
    const auto second = value.find('$', first == std::string::npos ? 0 : first + 1);
    const auto third = value.find('$', second == std::string::npos ? 0 : second + 1);
    if (first != 6 || value.substr(0, first) != "pbkdf2" || second == std::string::npos || third == std::string::npos) return false;
    const std::string iterations = value.substr(first + 1, second - first - 1);
    const std::string salt = value.substr(second + 1, third - second - 1);
    const std::string digest = value.substr(third + 1);
    if (iterations.empty() || salt.size() != 32 || digest.size() != 64 || !lowercaseHex(salt) || !lowercaseHex(digest)) return false;
    for (char c : iterations) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::optional<std::string> hashCanvasPassword(const std::string& password) {
    if (password.empty()) return std::nullopt;
    std::array<unsigned char, 16> salt{};
    std::array<unsigned char, 32> digest{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) return std::nullopt;
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), kIterations, EVP_sha256(),
                          static_cast<int>(digest.size()), digest.data()) != 1) return std::nullopt;
    return "pbkdf2$" + std::to_string(kIterations) + "$" + toHex(salt.data(), salt.size())
           + "$" + toHex(digest.data(), digest.size());
}

std::optional<std::string> normalizeCanvasPassword(const std::string& stored) {
    if (stored.empty()) return std::string{};
    return isCanvasPasswordHash(stored) ? std::optional<std::string>(stored) : hashCanvasPassword(stored);
}
