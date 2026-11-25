export module Topic;

import std;
export import SubscriptionPolicies;
export import StoragePolicies;

// Each Topid stores subscribers for exactly one event/message type.
// Ideal for single-threaded, high-frequency, or policy-enforced local pipelines
// where DataBus's mutexes and std::type_index lookups are unnecessary.
export template 
<typename MessageT,template <typename> class Container = VectorPolicy,std::derived_from<SubscriptionPolicy> SubscriptionPolicyT = NoopSubscriptionPolicy>
class Topic {
private:
    struct Subscriber_T {
        std::string identifier;
        std::move_only_function<void(const MessageT&) const> callback;
    };

    Container<Subscriber_T> subscribers;
    [[no_unique_address]] SubscriptionPolicyT subscriptionPolicy;

public:
    using message_type = MessageT;

    Topic() = default;
    explicit Topic(SubscriptionPolicyT policy)
        : subscriptionPolicy(std::move(policy)) {}

    template <typename Fn>
        requires std::invocable<const std::decay_t<Fn>&, const MessageT&>
    void subscribe(std::string_view identifier, Fn&& callback) {
        subscriptionPolicy.subscribe(identifier);
        subscribers.add(Subscriber_T{
            std::string{identifier},
            std::move_only_function<void(const MessageT&) const>{std::forward<Fn>(callback)}
        });
    }

    void unsubscribe(std::string_view identifier) {
        subscribers.remove_if([identifier](const Subscriber_T& subscriber) {
            return subscriber.identifier == identifier;
        });
        subscriptionPolicy.unsubscribe(identifier);
    }

    void publish(const MessageT& message) const {
        subscribers.for_each([&](const Subscriber_T& subscriber) {
            subscriber.callback(message);
        });
    }

    [[nodiscard]] bool empty() const noexcept {
        if constexpr (requires { subscribers.empty(); }) {
            return subscribers.empty();
        } else {
            return subscriberCount() == 0;
        }
    }

    [[nodiscard]] std::size_t subscriberCount() const noexcept {
        if constexpr (requires { subscribers.size(); }) {
            return subscribers.size();
        } else {
            std::size_t count = 0;
            subscribers.for_each([&](const Subscriber_T&) { ++count; });
            return count;
        }
    }
};
