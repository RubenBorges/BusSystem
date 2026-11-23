#include <print>
#include <iostream>
#include <logserver.hpp>

namespace SYS::logserver {
    void logMessage(const std::string& message) {
        std::cout << "[LOG]: " << message << std::endl;
    }
}
