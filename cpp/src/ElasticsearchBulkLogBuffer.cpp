#include "ElasticsearchBulkLogBuffer.hpp"
#include "ElasticsearchHttpClient.hpp"
#include "Environment.hpp"

#include <httplib.h>
#include <openssl/rand.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace {
constexpr std::size_t MAX_QUEUE_SIZE = 10'000;

void writeDiagnostic(const std::string& message) {
    std::size_t offset = 0;
    while (offset < message.size()) {
        const ssize_t written = ::write(STDERR_FILENO, message.data() + offset, message.size() - offset);
        if (written <= 0) return;
        offset += static_cast<std::size_t>(written);
    }
}

std::string envOr(const char* name, const std::string& fallback) {
    const auto value = environmentValue(name);
    return value.empty() ? fallback : value;
}

int envPort(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    try {
        const int parsed = std::stoi(value);
        return parsed > 0 && parsed <= 65535 ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

int envBoundedInt(const char* name, int fallback, int minimum, int maximum) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    try {
        return std::clamp(std::stoi(value), minimum, maximum);
    } catch (...) {
        return fallback;
    }
}

std::string newEventId() {
    unsigned char bytes[16]{};
    if (RAND_bytes(bytes, sizeof(bytes)) == 1) {
        std::ostringstream id;
        id << std::hex << std::setfill('0');
        for (unsigned char byte : bytes) id << std::setw(2) << static_cast<unsigned int>(byte);
        return id.str();
    }

    static std::atomic<std::uint64_t> sequence{0};
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(now) + "-" + std::to_string(getpid()) + "-"
        + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

std::int64_t nowEpochMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

bool acceptedBulkResponse(const std::string& body, std::size_t expected_count) {
    try {
        const auto response = nlohmann::json::parse(body);
        if (!response.contains("items") || !response["items"].is_array()
            || response["items"].size() != expected_count) return false;
        for (const auto& item : response["items"]) {
            if (!item.is_object() || !item.contains("create")) return false;
            const auto& result = item["create"];
            if (!result.contains("status") || !result["status"].is_number_integer()) return false;
            const int status = result["status"].get<int>();
            // A retry of a timed-out create action may find that the first
            // request already stored the same stable event ID.
            if (status != 200 && status != 201 && status != 409) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
}

ElasticsearchBulkLogBuffer& ElasticsearchBulkLogBuffer::instance() {
    static ElasticsearchBulkLogBuffer buffer;
    return buffer;
}

ElasticsearchBulkLogBuffer::ElasticsearchBulkLogBuffer()
    : host_(envOr("ES_HOST", "127.0.0.1")),
      port_(envPort("ES_PORT", 9200)),
      index_(envOr("ES_LOG_INDEX", "agora-logs")),
      username_(envOr("ES_LOG_USER_NAME", "agora_log_writer")),
      password_(envOr("ES_LOG_USER_PASSWORD", "")),
      batch_size_(static_cast<std::size_t>(envBoundedInt("ES_LOG_BATCH_SIZE", 100, 1, 1000))),
      flush_interval_ms_(envBoundedInt("ES_LOG_FLUSH_INTERVAL_MS", 1000, 100, 60000)) {
    configured_ = !host_.empty() && !index_.empty() && !username_.empty() && !password_.empty();
    if (!configured_) {
        writeDiagnostic("[ElasticsearchBulkLogBuffer] Disabled: set ES_HOST, ES_LOG_INDEX, "
                        "ES_LOG_USER_NAME and ES_LOG_USER_PASSWORD\n");
        return;
    }
    worker_ = std::thread([this]() { run(); });
}

ElasticsearchBulkLogBuffer::~ElasticsearchBulkLogBuffer() {
    if (!configured_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void ElasticsearchBulkLogBuffer::record(const std::string& component, const std::string& event,
                                        const std::string& level, const std::string& message,
                                        const nlohmann::json& details) {
    if (!configured_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    enqueueLocked(component, event, level, message, details);
}

bool ElasticsearchBulkLogBuffer::reportAvailability(const std::string& component, bool healthy,
                                                     const nlohmann::json& details) {
    if (!configured_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous = availability_.find(component);
    if (previous == availability_.end()) {
        availability_[component] = healthy;
        if (!healthy) {
            enqueueLocked(component, "storage_unavailable", "ERROR",
                          component + " became unavailable", details);
            return true;
        }
        return false;
    }
    if (previous->second == healthy) return false;
    previous->second = healthy;
    enqueueLocked(component, healthy ? "storage_recovered" : "storage_unavailable",
                  healthy ? "INFO" : "ERROR",
                  healthy ? component + " connection recovered" : component + " became unavailable",
                  details);
    return true;
}

bool ElasticsearchBulkLogBuffer::reportPrimaryChange(const std::string& component,
                                                      const std::string& primary) {
    if (!configured_ || primary.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous = primaries_.find(component);
    if (previous == primaries_.end()) {
        primaries_[component] = primary;
        return false;
    }
    if (previous->second == primary) return false;
    const std::string old_primary = previous->second;
    previous->second = primary;
    enqueueLocked(component, "primary_changed", "WARN",
                  component + " primary changed after failover",
                  {{"previous_primary", old_primary}, {"current_primary", primary}});
    return true;
}

void ElasticsearchBulkLogBuffer::enqueueLocked(const std::string& component, const std::string& event,
                                                 const std::string& level, const std::string& message,
                                                 const nlohmann::json& details) {
    if (queue_.size() >= MAX_QUEUE_SIZE) {
        if (!overflow_warned_) {
            writeDiagnostic("[ElasticsearchBulkLogBuffer] Queue is full; application logs are being dropped\n");
            overflow_warned_ = true;
        }
        return;
    }

    const std::string id = newEventId();
    nlohmann::json document = {
        {"@timestamp", nowEpochMillis()},
        {"event_id", id},
        {"service", "agora-cpp"},
        {"instance", envOr("HOSTNAME", "unknown")},
        {"component", component},
        {"event", event},
        {"level", level},
        {"message", message}
    };
    if (!details.is_null() && !details.empty()) document["details"] = details;
    queue_.push_back({id, std::move(document)});
    cv_.notify_one();
}

void ElasticsearchBulkLogBuffer::run() {
    while (true) {
        std::vector<LogEvent> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(flush_interval_ms_), [this]() {
                return stopping_ || queue_.size() >= batch_size_;
            });
            if (queue_.empty() && stopping_) return;
            const std::size_t count = std::min(queue_.size(), batch_size_);
            batch.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                batch.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
            if (queue_.size() < MAX_QUEUE_SIZE / 2) overflow_warned_ = false;
        }
        if (!batch.empty() && !sendBatch(batch)) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stopping_) return;
            for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                while (queue_.size() >= MAX_QUEUE_SIZE) {
                    queue_.pop_back();
                    if (!overflow_warned_) {
                        writeDiagnostic("[ElasticsearchBulkLogBuffer] Queue is full during retry; newer logs are being dropped\n");
                    }
                    overflow_warned_ = true;
                }
                queue_.push_front(*it);
            }
            cv_.wait_for(lock, std::chrono::milliseconds(flush_interval_ms_), [this]() {
                return stopping_;
            });
            if (stopping_) return;
        }
    }
}

bool ElasticsearchBulkLogBuffer::sendBatch(const std::vector<LogEvent>& batch) {
    auto client = makeElasticsearchHttpClient(host_, port_);
    if (!client) return false;
    client->set_connection_timeout(2, 0);
    client->set_read_timeout(3, 0);
    client->set_write_timeout(3, 0);
    client->set_basic_auth(username_, password_);

    std::string payload;
    for (const auto& event : batch) {
        payload += nlohmann::json{{"create", {{"_id", event.id}}}}.dump();
        payload.push_back('\n');
        payload += event.document.dump();
        payload.push_back('\n');
    }
    const std::string path = "/" + index_ + "/_bulk?refresh=false";

    for (int attempt = 0; attempt < 3; ++attempt) {
        auto response = client->Post(path, payload, "application/x-ndjson");
        if (response && response->status >= 200 && response->status < 300
            && acceptedBulkResponse(response->body, batch.size())) {
            return true;
        }

        if (response && response->status >= 400 && response->status < 500 && response->status != 429) {
            writeDiagnostic("[ElasticsearchBulkLogBuffer] Bulk write rejected (HTTP "
                            + std::to_string(response->status) + ")\n");
            return false;
        }
        if (attempt < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
        }
    }

    writeDiagnostic("[ElasticsearchBulkLogBuffer] Bulk write failed after bounded retries (events="
                    + std::to_string(batch.size()) + ")\n");
    return false;
}

ElasticsearchLogStreamCapture::CaptureBuffer::CaptureBuffer(
        ElasticsearchBulkLogBuffer& sink, std::streambuf* destination,
        std::string stream_name, bool error_stream)
    : sink_(sink), destination_(destination), stream_name_(std::move(stream_name)),
      error_stream_(error_stream) {}

ElasticsearchLogStreamCapture::CaptureBuffer::int_type
ElasticsearchLogStreamCapture::CaptureBuffer::overflow(int_type character) {
    if (traits_type::eq_int_type(character, traits_type::eof())) return traits_type::not_eof(character);
    const char value = traits_type::to_char_type(character);
    return xsputn(&value, 1) == 1 ? character : traits_type::eof();
}

std::streamsize ElasticsearchLogStreamCapture::CaptureBuffer::xsputn(const char* text, std::streamsize count) {
    if (count <= 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::streamsize written = destination_->sputn(text, count);
    if (written > 0) capture(text, static_cast<std::size_t>(written));
    return written;
}

int ElasticsearchLogStreamCapture::CaptureBuffer::sync() {
    std::lock_guard<std::mutex> lock(mutex_);
    // std::cerr is unit-buffered, so flushing here would split one streamed
    // message into many documents. Complete a log only at newline or shutdown.
    return destination_->pubsync();
}

void ElasticsearchLogStreamCapture::CaptureBuffer::capture(const char* text, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (text[i] == '\n') {
            if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
            emitLine(std::move(pending_));
            pending_.clear();
        } else {
            pending_.push_back(text[i]);
        }
    }
}

void ElasticsearchLogStreamCapture::CaptureBuffer::emitLine(std::string line) {
    if (line.empty()) return;
    std::string component = "application";
    if (line.front() == '[') {
        const std::size_t end = line.find(']');
        if (end > 1 && end <= 80) component = line.substr(1, end - 1);
    }

    std::string level = error_stream_ ? "ERROR" : "INFO";
    if (error_stream_ && (line.find("WARN") != std::string::npos
                          || line.find("Warning") != std::string::npos)) level = "WARN";
    sink_.record(component, "application_log", level, line,
                 {{"stream", stream_name_}});
}

void ElasticsearchLogStreamCapture::CaptureBuffer::flushPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_.empty()) {
        emitLine(std::move(pending_));
        pending_.clear();
    }
}

ElasticsearchLogStreamCapture::ElasticsearchLogStreamCapture(ElasticsearchBulkLogBuffer& sink)
    : original_stdout_(std::cout.rdbuf()), original_stderr_(std::cerr.rdbuf()),
      stdout_buffer_(sink, original_stdout_, "stdout", false),
      stderr_buffer_(sink, original_stderr_, "stderr", true) {
    std::cout.rdbuf(&stdout_buffer_);
    std::cerr.rdbuf(&stderr_buffer_);
}

ElasticsearchLogStreamCapture::~ElasticsearchLogStreamCapture() {
    std::cout.rdbuf(original_stdout_);
    std::cerr.rdbuf(original_stderr_);
    stdout_buffer_.flushPending();
    stderr_buffer_.flushPending();
}
