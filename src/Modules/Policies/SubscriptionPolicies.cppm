export module SubscriptionPolicies;

import std;

// SUBSCRIPTION TYPE POLICIES for MessageBus
export struct SubscriptionPolicy {
    // Defines the lifecycle action for creating a named subscription.
    virtual void subscribe(std::string_view subscriberId) = 0;
    // Defines the lifecycle action for removing a named subscription.
    virtual void unsubscribe(std::string_view subscriberId) = 0;

    // A virtual destructor ensures derived policies are cleaned up correctly.
    virtual ~SubscriptionPolicy() = default;
};

export struct LogSubscriptionPolicy : public SubscriptionPolicy {
    // The policy owns the log file used for subscription lifecycle messages.
    std::ofstream logFile{"log.txt", std::ios::app};

    void subscribe(std::string_view subscriberId) override {
        // Fail with a message instead of silently writing to a closed file.
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file." << std::endl;
            return;
        }
        // Record the exact subscriber identifier that created the subscription.
        auto now = std::chrono::system_clock::now();
        logFile << std::format("T {:%T} : Subscribed: {} \n", now, subscriberId);
    }

    void unsubscribe(std::string_view subscriberId) override {
        // Record the exact subscriber identifier that removed the subscription.
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            logFile << std::format("T {:%T} : Unsubscribed: {} \n", now, subscriberId);
        }
    }
};

export struct NotifySubscriptionPolicy : public SubscriptionPolicy {
    void subscribe(std::string_view subscriberId) override {
        // Print the exact subscriber identifier that created the subscription.
        std::cout << "Subscribed: " << subscriberId << '\n';
    }

    void unsubscribe(std::string_view subscriberId) override {
        // Print the exact subscriber identifier that removed the subscription.
        std::cout << "Unsubscribed: " << subscriberId << '\n';
    }
};


export struct NoopSubscriptionPolicy : public SubscriptionPolicy {
    // This default policy performs no action for applications that need no lifecycle behavior.
    void subscribe(std::string_view) override {}
    // This default policy performs no action for applications that need no lifecycle behavior.
    void unsubscribe(std::string_view) override {}
};

//---------------------------------------------------------------
