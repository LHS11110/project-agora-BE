#pragma once

#include <iostream>
#include <sstream>
#include <stdexcept>

#define AGORA_CHECK(condition)                                                        \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::ostringstream message;                                               \
            message << __FILE__ << ':' << __LINE__ << ": check failed: " #condition; \
            throw std::runtime_error(message.str());                                 \
        }                                                                             \
    } while (false)

template <typename TestFunction>
int runTest(const char* name, TestFunction test) {
    try {
        test();
        std::cout << "[PASS] " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        return 1;
    }
}
