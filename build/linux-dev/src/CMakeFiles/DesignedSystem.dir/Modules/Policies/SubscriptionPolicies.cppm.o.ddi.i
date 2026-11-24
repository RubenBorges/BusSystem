# 0 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Policies/SubscriptionPolicies.cppm"
# 1 "/home/boopy/dev/Projects/DesignedSystem/build/linux-dev//"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Policies/SubscriptionPolicies.cppm"
export module SubscriptionPolicies;

import std;


export struct SubscriptionPolicy {

    virtual void subscribe(std::string_view subscriberId) = 0;

    virtual void unsubscribe(std::string_view subscriberId) = 0;


    virtual ~SubscriptionPolicy() = default;
};

export struct LogSubscriptionPolicy : public SubscriptionPolicy {

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

export struct NotifySubscriptionPolicy : public SubscriptionPolicy {
    void subscribe(std::string_view subscriberId) override {

        std::cout << "Subscribed: " << subscriberId << '\n';
    }

    void unsubscribe(std::string_view subscriberId) override {

        std::cout << "Unsubscribed: " << subscriberId << '\n';
    }
};


export struct NoopSubscriptionPolicy : public SubscriptionPolicy {

    void subscribe(std::string_view) override {}

    void unsubscribe(std::string_view) override {}
};
