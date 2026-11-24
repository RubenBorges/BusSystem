module;
import std; 
export module Policies;

// SUBSCRIPTION TYPE POLICIES for MessageBus
struct SubscriptionPolicy {
    // Defines the lifecycle action for creating a named subscription.
    virtual void subscribe(std::string_view subscriberId) = 0;
    // Defines the lifecycle action for removing a named subscription.
    virtual void unsubscribe(std::string_view subscriberId) = 0;

    // A virtual destructor ensures derived policies are cleaned up correctly.
    virtual ~SubscriptionPolicy() = default;
};

struct LogSubscriptionPolicy : public SubscriptionPolicy {
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

struct NotifySubscriptionPolicy : public SubscriptionPolicy {
    void subscribe(std::string_view subscriberId) override {
        // Print the exact subscriber identifier that created the subscription.
        std::cout << "Subscribed: " << subscriberId << '\n';
    }

    void unsubscribe(std::string_view subscriberId) override {
        // Print the exact subscriber identifier that removed the subscription.
        std::cout << "Unsubscribed: " << subscriberId << '\n';
    }
};


struct NoopSubscriptionPolicy : public SubscriptionPolicy {
    // This default policy performs no action for applications that need no lifecycle behavior.
    void subscribe(std::string_view) override {}
    // This default policy performs no action for applications that need no lifecycle behavior.
    void unsubscribe(std::string_view) override {}
};

//---------------------------------------------------------------

// STORAGE POLICIES for the MessageBus

template <typename T>
struct VectorPolicy {
    // Stores all values managed by this policy in a vector.
    std::vector<T> items;

    void add(T item) {
        // Move the item so callbacks and other movable values are stored efficiently.
        items.push_back(std::move(item));
    }

    template <typename Predicate>
    void remove_if(Predicate&& predicate) {
        // Erase subscribers whose identifiers match the supplied predicate.
        std::erase_if(items, std::forward<Predicate>(predicate));
    }

    template <typename Function>
    void for_each(Function&& function) const {
        // Iterate through subscribers without exposing the storage implementation.
        for (const auto& item : items) {
            function(item);
        }
    }
};
