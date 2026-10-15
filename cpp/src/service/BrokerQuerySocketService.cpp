#include "service/BrokerQuerySocketService.hpp"
#include <openssl/crypto.h>
#include <iostream>

BrokerQuerySocketService::BrokerQuerySocketService(CanvasQueryService& queries, std::string host, int port, std::string token, Executor executor)
    : executor_(std::move(executor)), queries_(queries), host_(std::move(host)), token_(std::move(token)), port_(port) {}
BrokerQuerySocketService::~BrokerQuerySocketService() { stop(); }
bool BrokerQuerySocketService::start() {
    accepting_ = true;
    for (int index = 0; index < 16; ++index) workers_.emplace_back([this] { worker(); });
    listener_ = std::thread([this] { run(); });
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return setup_; });
    return bound_;
}
void BrokerQuerySocketService::stop() {
    accepting_ = false;
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    work_.notify_all();
    for (auto& thread : workers_) if (thread.joinable()) thread.join();
    workers_.clear();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_) loop_->defer([this] {
            if (listener_socket_) { us_listen_socket_close(1, static_cast<us_listen_socket_t*>(listener_socket_)); listener_socket_ = nullptr; }
            // close callbacks erase the map; don't iterate it while closing.
            while (!connections_.empty()) connections_.begin()->second->end(1012, "Query server restarting");
        });
    }
    if (listener_.joinable()) listener_.join();
}
void BrokerQuerySocketService::run() {
    auto app = BrokerSocketApi::create();
    { std::lock_guard<std::mutex> lock(mutex_); loop_ = BrokerSocketApi::Loop::get(); }
    BrokerSocketApi::App::WebSocketBehavior<BrokerConnection> behavior;
    behavior.compression = uWS::DISABLED;
    behavior.maxPayloadLength = 12 * 1024 * 1024;
    behavior.maxBackpressure = 16 * 1024 * 1024;
    behavior.closeOnBackpressureLimit = false;
    behavior.idleTimeout = 60;
    behavior.sendPingsAutomatically = true;
    behavior.upgrade = [this](auto* response, auto* request, auto* context) {
        const auto supplied = request->getHeader("x-agora-internal-token");
        if (!accepting_ || connections_.size() >= 16 || token_.size() < 32 || supplied.size() != token_.size()
            || CRYPTO_memcmp(supplied.data(), token_.data(), token_.size()) != 0) {
            response->writeStatus("401 Unauthorized")->end("Broker authentication required"); return;
        }
        response->template upgrade<BrokerConnection>({++next_connection_}, request->getHeader("sec-websocket-key"),
            request->getHeader("sec-websocket-protocol"), request->getHeader("sec-websocket-extensions"), context);
    };
    behavior.open = [this](auto* socket) { connections_[socket->getUserData()->id] = socket; };
    behavior.close = [this](auto* socket, int, std::string_view) {
        const auto id=socket->getUserData()->id;
        for(const auto& reply:replies_[id])reply_bytes_-=reply.data.size();
        replies_.erase(id);connections_.erase(id);
    };
    behavior.drain = [this](auto* socket) { drainReplies(socket->getUserData()->id); };
    behavior.message = [this](auto* socket, std::string_view payload, uWS::OpCode opcode) {
        if (opcode != uWS::TEXT || !accepting_) { socket->end(1008, "Broker text query required"); return; }
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= 128 || queued_bytes_ + payload.size() > 32 * 1024 * 1024) {
            try {
                auto route=nlohmann::json::parse(payload).at("route");
                auto reply=nlohmann::json{{"route",route},{"response",{{"type","error"},{"code","BROKER_BUSY"},{"request_id",route.value("request_id","")}}}}.dump();
                loop_->defer([this,id=socket->getUserData()->id,reply=std::move(reply)]{enqueueReply(id,reply);});
            }catch(...) { /* Invalid internal requests cannot be correlated. */ }
            return;
        }
        queued_bytes_ += payload.size();
        queue_.push_back({socket->getUserData()->id, std::string(payload)});
        work_.notify_one();
    };
    app->ws<BrokerConnection>("/broker/queries", std::move(behavior));
    app->listen(host_, port_, LIBUS_LISTEN_EXCLUSIVE_PORT, [this](auto* token) {
        std::lock_guard<std::mutex> lock(mutex_);
        listener_socket_ = token; setup_ = true; bound_ = token != nullptr; ready_.notify_all();
    });
    if (bound_) app->run();
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr; listener_socket_ = nullptr;
}
void BrokerQuerySocketService::worker() {
    for (;;) {
        Work entry;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) { if (stopping_) return; continue; }
            entry = std::move(queue_.front()); queue_.pop_front(); queued_bytes_ -= entry.payload.size();
        }
        std::string reply;
        try {
            auto input = nlohmann::json::parse(entry.payload);
            reply = (executor_ ? executor_(input) : queries_.execute(input)).dump();
        } catch (...) {
            // A malformed envelope cannot be correlated safely.
            reply.clear();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_) loop_->defer([this, connection = entry.connection, reply = std::move(reply)]() mutable {
            enqueueReply(connection,std::move(reply));
        });
    }
}

void BrokerQuerySocketService::enqueueReply(std::uint64_t connection, std::string reply) {
    if(!connections_.count(connection) || reply.empty())return;
    if(reply.size()>128*1024*1024 || reply_bytes_+reply.size()>256*1024*1024){
        try {
            const auto route=nlohmann::json::parse(reply).at("route");
            reply=nlohmann::json{{"route",route},{"response",{{"type","error"},{"code","BROKER_BUSY"},{"request_id",route.value("request_id","")}}}}.dump();
        }catch(...){return;}
    }
    reply_bytes_+=reply.size();replies_[connection].push_back({std::move(reply),0});
    drainReplies(connection);
}

void BrokerQuerySocketService::drainReplies(std::uint64_t connection) {
    const auto found=connections_.find(connection);if(found==connections_.end())return;
    auto* socket=found->second;auto& queue=replies_[connection];
    while(!queue.empty() && socket->getBufferedAmount()<256*1024){
        auto& reply=queue.front();const auto count=std::min<std::size_t>(128*1024,reply.data.size()-reply.offset);
        const std::string_view chunk(reply.data.data()+reply.offset,count);
        const bool first=reply.offset==0,last=reply.offset+count==reply.data.size();
        const auto status=first ? (last ? socket->send(chunk,uWS::TEXT) : socket->sendFirstFragment(chunk,uWS::TEXT))
            : (last ? socket->sendLastFragment(chunk) : socket->sendFragment(chunk));
        if(status==BrokerSocketApi::Socket::DROPPED)return;
        reply.offset+=count;
        if(last){reply_bytes_-=reply.data.size();queue.pop_front();}
        if(status==BrokerSocketApi::Socket::BACKPRESSURE)return;
    }
}
