// TLS integration fixture: invalid capabilities exercise the real transport without storage.
#include "service/BrokerQuerySocketService.hpp"
#include <chrono>
#include <thread>
#include <iostream>
int main() {
    CanvasLifecycleMemory memory;
    CanvasQueryService queries(memory, 8002);
    BrokerQuerySocketService server(queries, "0.0.0.0", 9448, "integration-test-token-32-bytes-long");
    if (!server.start()) return 1;
    std::cout << "TLS socket fixture ready" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(120));
    server.stop();
}
