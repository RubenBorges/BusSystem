#include <iostream>
#include <fstream>
#include <print>
#include <format>
#include <chrono>

// Containers
#include <vector>
#include <array>
#include <list>
#include <unordered_map>
#include <unordered_set>

#include <functional>
#include <ranges>
#include <expected>
#include <optional>
#include <algorithm>
#include <utility>
#include <memory>
//SUBSCRIPTIONTYPE POLICIES for MessageBus
struct SubscriptionPolicy {
    virtual void subscribe() = 0;
    virtual void unsubscribe() = 0;

    virtual ~SubscriptionPolicy() = default;
};

struct LogSubscriptionPolicy : public SubscriptionPolicy{
    std::ofstream logFile{"log.txt", std::ios::app};

    void subscribe() override {
    // Implementation for subscribing to log messages
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file." << std::endl;
        return;
    }
    logFile << "Subscribed to log messages.\n" << std::endl;
    };

    void unsubscribe() override {
    // Implementation for unsubscribing from log messages
    logFile << "Unsubscribed from log messages.\n" << std::endl;
    logFile.close();
    std::cout << "Unsubscribed from log messages." << std::endl;

    }
};

//---------------------------------------------------------------

//STORAGE POLICIES for the MessageBus
template <typename T>
struct VectorPolicy {
    std::vector<T> items;

    void add(T item) {
        items.push_back(std::move(item));
    }

    template <typename Function>
    void for_each(Function&& function) const {
        for (const auto& item : items) {
            function(item);
        }
    }
};
//--------------------------------

template <typename MessageT, template <typename> class Storage = VectorPolicy>
class MessageBus {
private:
    using Subscriber = std::function<void(const MessageT&)>;

    Storage<Subscriber> subscribers;

public:
    void subscribe(Subscriber subscriber) {
        subscribers.add(std::move(subscriber));
        LogSubscriptionPolicy().subscribe();
    }

    void publish(const MessageT& message) {
        subscribers.for_each([&](const Subscriber& subscriber) {
            subscriber(message);
        });
        LogSubscriptionPolicy().unsubscribe();
    }
};

