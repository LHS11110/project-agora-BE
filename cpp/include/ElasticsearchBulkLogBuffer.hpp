#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <nlohmann/json.hpp>

/** Asynchronous append-only application event writer using Elasticsearch _bulk. */
class ElasticsearchBulkLogBuffer {
public:
    static ElasticsearchBulkLogBuffer& instance();
    ~ElasticsearchBulkLogBuffer();

    void record(const std::string& component, const std::string& event,
                const std::string& level, const std::string& message,
                const nlohmann::json& details = nlohmann::json::object());
    bool reportAvailability(const std::string& component, bool healthy,
                            const nlohmann::json& details = nlohmann::json::object());
    bool reportPrimaryChange(const std::string& component, const std::string& primary);

private:
    struct LogEvent {
        std::string id;
        nlohmann::json document;
    };

    ElasticsearchBulkLogBuffer();
    void enqueueLocked(const std::string& component, const std::string& event,
                       const std::string& level, const std::string& message,
                       const nlohmann::json& details);
    void run();
    bool sendBatch(const std::vector<LogEvent>& batch);

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<LogEvent> queue_;
    std::map<std::string, bool> availability_;
    std::map<std::string, std::string> primaries_;
    std::thread worker_;
    bool stopping_ = false;
    bool configured_ = false;
    bool overflow_warned_ = false;
    std::string host_;
    int port_ = 9200;
    std::string index_;
    std::string username_;
    std::string password_;
    std::size_t batch_size_ = 100;
    int flush_interval_ms_ = 1000;
};

/** Mirrors the application's stdout/stderr lines to the asynchronous ES sink. */
class ElasticsearchLogStreamCapture {
public:
    explicit ElasticsearchLogStreamCapture(ElasticsearchBulkLogBuffer& sink);
    ~ElasticsearchLogStreamCapture();

    ElasticsearchLogStreamCapture(const ElasticsearchLogStreamCapture&) = delete;
    ElasticsearchLogStreamCapture& operator=(const ElasticsearchLogStreamCapture&) = delete;

private:
    class CaptureBuffer : public std::streambuf {
    public:
        CaptureBuffer(ElasticsearchBulkLogBuffer& sink, std::streambuf* destination,
                      std::string stream_name, bool error_stream);
        void flushPending();

    protected:
        int_type overflow(int_type character) override;
        std::streamsize xsputn(const char* text, std::streamsize count) override;
        int sync() override;

    private:
        void capture(const char* text, std::size_t count);
        void emitLine(std::string line);

        ElasticsearchBulkLogBuffer& sink_;
        std::streambuf* destination_;
        std::string stream_name_;
        bool error_stream_;
        std::mutex mutex_;
        std::string pending_;
    };

    std::streambuf* original_stdout_;
    std::streambuf* original_stderr_;
    CaptureBuffer stdout_buffer_;
    CaptureBuffer stderr_buffer_;
};
