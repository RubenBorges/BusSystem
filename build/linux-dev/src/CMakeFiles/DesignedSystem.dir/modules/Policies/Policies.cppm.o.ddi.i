# 0 "/home/boopy/dev/Projects/DesignedSystem/src/modules/Policies/Policies.cppm"
# 1 "/home/boopy/dev/Projects/DesignedSystem/build/linux-dev//"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "/home/boopy/dev/Projects/DesignedSystem/src/modules/Policies/Policies.cppm"
module;
import std;
export module Policies;


struct SubscriptionPolicy {

    virtual void subscribe(std::string_view subscriberId) = 0;

    virtual void unsubscribe(std::string_view subscriberId) = 0;


    virtual ~SubscriptionPolicy() = default;
};

struct LogSubscriptionPolicy : public SubscriptionPolicy {

    std::ofstream logFile{"log.txt", std::ios::app};

    void subscribe(std::string_view subscriberId) override {

        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file." << std::endl;
            return;
        }

        auto now = std::chrono::system_clock::now();
        logFile << std::format("T {:%T} : Subscribed: {} \n", now, subscriberId);
    }

    void unsubscribe(std::string_view subscriberId) override {

        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            logFile << std::format("T {:%T} : Unsubscribed: {} \n", now, subscriberId);
        }
    }
};

struct NotifySubscriptionPolicy : public SubscriptionPolicy {
    void subscribe(std::string_view subscriberId) override {

        std::cout << "Subscribed: " << subscriberId << '\n';
    }

    void unsubscribe(std::string_view subscriberId) override {

        std::cout << "Unsubscribed: " << subscriberId << '\n';
    }
};


struct NoopSubscriptionPolicy : public SubscriptionPolicy {

    void subscribe(std::string_view) override {}

    void unsubscribe(std::string_view) override {}
};





template <typename T>
struct VectorPolicy {

    std::vector<T> items;

    void add(T item) {

        items.push_back(std::move(item));
    }

    template <typename Predicate>
    void remove_if(Predicate&& predicate) {

        std::erase_if(items, std::forward<Predicate>(predicate));
    }

    template <typename Function>
    void for_each(Function&& function) const {

        for (const auto& item : items) {
            function(item);
        }
    }
};
