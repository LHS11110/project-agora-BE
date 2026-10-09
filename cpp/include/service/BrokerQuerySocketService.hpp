#pragma once
#include "api/BrokerSocketApi.hpp"
#include "service/CanvasQueryService.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <unordered_map>
#include <atomic>
class BrokerQuerySocketService final {
public:
    BrokerQuerySocketService(CanvasQueryService& queries, std::string host, int port, std::string token);
    ~BrokerQuerySocketService();
    bool start();
    void stop();
private:
    struct Work { std::uint64_t connection; std::string payload; };
    void run();
    void worker();
    CanvasQueryService& queries_;
    std::string host_, token_; int port_;
    std::thread listener_; std::vector<std::thread> workers_;
    std::mutex mutex_; std::condition_variable ready_, work_;
    BrokerSocketApi::Loop* loop_{nullptr}; void* listener_socket_{nullptr};
    bool setup_{false}, bound_{false}, stopping_{false};
    std::size_t queued_bytes_{0}; std::deque<Work> queue_;
    std::atomic<bool> accepting_{false};
    std::uint64_t next_connection_{0};
    std::unordered_map<std::uint64_t, BrokerSocketApi::Socket*> connections_;
};
