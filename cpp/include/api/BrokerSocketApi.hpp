#pragma once
#include "App.h"
#include <memory>
#include <cstdint>
struct BrokerConnection { std::uint64_t id{0}; };
class BrokerSocketApi final {
public:
    using App = uWS::SSLApp;
    using Socket = uWS::WebSocket<true, true, BrokerConnection>;
    using Loop = uWS::Loop;
    static std::unique_ptr<App> create();
};
