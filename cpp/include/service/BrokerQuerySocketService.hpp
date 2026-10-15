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
#include <functional>
class BrokerQuerySocketService final {
public:
    using Executor = std::function<nlohmann::json(const nlohmann::json&)>;
    BrokerQuerySocketService(CanvasQueryService& queries, std::string host, int port, std::string token, Executor executor = {});
    ~BrokerQuerySocketService();
    bool start();
    void stop();
private:
    struct Work { std::uint64_t connection; std::string payload; };
    void run();
    void worker();
    void enqueueReply(std::uint64_t connection, std::string reply);
    void drainReplies(std::uint64_t connection);
    struct Reply { std::string data; std::size_t offset{0}; };
    std::unordered_map<std::uint64_t, std::deque<Reply>> replies_;
    std::size_t reply_bytes_{0};
    Executor executor_;
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
