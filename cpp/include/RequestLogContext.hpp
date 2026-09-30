#pragma once

#include <openssl/rand.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <unistd.h>

namespace agora::logging {

struct RequestLogContext {
    std::string request_id;
    std::string operation;
    std::string outcome;
    std::string error_code;
    std::chrono::steady_clock::time_point started_at{};
    std::string parent_request_id;
};

inline thread_local RequestLogContext current_context;

inline bool isValidRequestId(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    for (unsigned char character : value) {
        if (!(std::isalnum(character) || character == '.' || character == '_' || character == ':' || character == '-')) {
            return false;
        }
    }
    return true;
}

inline std::string generateRequestId() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) == 1) {
        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (const auto byte : bytes) result << std::setw(2) << static_cast<unsigned int>(byte);
        return result.str();
    }
    static std::atomic<std::uint64_t> sequence{0};
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(now) + "-" + std::to_string(getpid()) + "-"
        + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

inline std::string acceptedRequestId(std::string_view supplied) {
    return isValidRequestId(supplied) ? std::string(supplied) : generateRequestId();
}

inline RequestLogContext& currentRequestLogContext() {
    return current_context;
}

inline void setCurrentRequestLogContext(RequestLogContext context) {
    current_context = std::move(context);
}

inline void clearCurrentRequestLogContext() {
    current_context = {};
}

inline void markCurrentRequestFailure(std::string error_code = {}) {
    if (current_context.request_id.empty()) return;
    current_context.outcome = "failure";
    current_context.error_code = std::move(error_code);
}

inline void markCurrentRequestRejected(std::string error_code = {}) {
    if (current_context.request_id.empty()) return;
    current_context.outcome = "rejected";
    current_context.error_code = std::move(error_code);
}

inline void setCurrentRequestOutcome(std::string outcome) {
    if (!current_context.request_id.empty()) current_context.outcome = std::move(outcome);
}

class RequestLogContextScope {
public:
    explicit RequestLogContextScope(RequestLogContext context)
        : previous_(current_context) {
        current_context = std::move(context);
    }

    RequestLogContextScope(std::string request_id, std::string operation,
                           std::string parent_request_id = {})
        : RequestLogContextScope(RequestLogContext{
              std::move(request_id), std::move(operation), {}, {},
              std::chrono::steady_clock::now(), std::move(parent_request_id)}) {}

    ~RequestLogContextScope() {
        current_context = std::move(previous_);
    }

    RequestLogContextScope(const RequestLogContextScope&) = delete;
    RequestLogContextScope& operator=(const RequestLogContextScope&) = delete;

private:
    RequestLogContext previous_;
};

} // namespace agora::logging
