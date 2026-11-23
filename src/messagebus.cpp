#include "messagebus.hpp"
#include <iostream>
int main (){
std::cout<< "Starting MessageBus example..." << std::endl;
    MessageBus<LogSubscriptionPolicy> logBus;
    std::cout<< "Subscribing to log messages..." << std::endl;
    logBus.subscribe([](const LogSubscriptionPolicy& log) {
        std::cout << "Received log message 1." << std::endl;
    });
    logBus.subscribe([](const LogSubscriptionPolicy& log) {
        std::cout << "Received log message 2." << std::endl;
    });
    logBus.subscribe([](const LogSubscriptionPolicy& log) {
        std::cout << "Received log message 3." << std::endl;
    });
    std::cout<< "Publishing log message..." << std::endl;
    logBus.publish(LogSubscriptionPolicy());
    logBus.publish(LogSubscriptionPolicy());
    logBus.publish(LogSubscriptionPolicy());
    logBus.publish(LogSubscriptionPolicy());
    return 0;
}