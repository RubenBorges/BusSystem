# 0 "/home/boopy/dev/Projects/DesignedSystem/src/modules/MessageBus/MessageBus.cppm"
# 1 "/home/boopy/dev/Projects/DesignedSystem/build/linux-dev//"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "/home/boopy/dev/Projects/DesignedSystem/src/modules/MessageBus/MessageBus.cppm"
module;

import std;
export import SubscriptionPolicies;
export import StoragePolicies;

export module MessageBus;

export template <typename MessageT,
                 template <typename> class Container = VectorPolicy,
                 typename SubscriptionPolicyT = NoopSubscriptionPolicy>
class MessageBus {
private:
    struct Subscriber_T {
        std::string identifier;
        std::function<void(const MessageT&)> callback;
    };

    Container<Subscriber_T> subscribers;
    SubscriptionPolicyT subscriptionPolicy;

public:
    static_assert(std::is_base_of_v<SubscriptionPolicy, SubscriptionPolicyT>,
                  "SubscriptionPolicyT must derive from SubscriptionPolicy");

    void subscribe(std::string_view identifier,
                   std::function<void(const MessageT&)> callback) {
        subscribers.add(Subscriber_T{std::string{identifier}, std::move(callback)});
        subscriptionPolicy.subscribe(identifier);
    }

    void unsubscribe(std::string_view identifier) {
        subscribers.remove_if([identifier](const Subscriber_T& subscriber) {
            return subscriber.identifier == identifier;
        });
        subscriptionPolicy.unsubscribe(identifier);
    }

    void publish(const MessageT& message) {
        subscribers.for_each([&](const Subscriber_T& subscriber) {
            subscriber.callback(message);
        });
    }
};
