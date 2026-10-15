// TLS integration fixture: invalid capabilities exercise the real transport without storage.
#include "service/BrokerQuerySocketService.hpp"
#include <chrono>
#include <thread>
#include <iostream>
int main() {
    CanvasLifecycleMemory memory;
    CanvasQueryService queries(memory, 8002);
    BrokerQuerySocketService server(queries, "0.0.0.0", 9448, "integration-test-token-32-bytes-long",
        [&](const nlohmann::json& input){
            if(input.value("command","")=="test_large_reply"){
                return nlohmann::json{{"route",input.at("route")},{"response",{{"type","test_large_result"},{"data",std::string(20*1024*1024,'x')}}}};
            }
            return queries.execute(input);
        });
    if (!server.start()) return 1;
    std::cout << "TLS socket fixture ready" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(600));
    server.stop();
}
