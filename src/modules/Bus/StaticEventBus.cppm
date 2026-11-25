export module StaticEventBus;

import std;
export import Topic;

// StaticEventBus stores one compile-time Topic or TOPIC per distinct message type,
// dispatching via std::get<...> with zero hash lookups or type erasure.
export template <typename... MessageTs>
class StaticEventBus {
private:
    template <typename MessageT>
    static constexpr bool is_supported_v = (std::same_as<MessageT, MessageTs> || ...);

    std::tuple<Topic<MessageTs, VectorPolicy, NoopSubscriptionPolicy>...> channels;

public:
    template <typename MessageT>
        requires is_supported_v<MessageT>
    [[nodiscard]] constexpr Topic<MessageT, VectorPolicy, NoopSubscriptionPolicy>& channel() noexcept {
        return std::get<Topic<MessageT, VectorPolicy, NoopSubscriptionPolicy>>(channels);
    }

    template <typename MessageT>
        requires is_supported_v<MessageT>
    [[nodiscard]] constexpr const Topic<MessageT, VectorPolicy, NoopSubscriptionPolicy>& channel() const noexcept {
        return std::get<Topic<MessageT, VectorPolicy, NoopSubscriptionPolicy>>(channels);
    }

    template <typename MessageT, typename Fn>
        requires is_supported_v<MessageT> && std::invocable<const std::decay_t<Fn>&, const MessageT&>
    void subscribe(std::string_view identifier, Fn&& subscriber) {
        channel<MessageT>().subscribe(identifier, std::forward<Fn>(subscriber));
    }

    template <typename MessageT>
        requires is_supported_v<MessageT>
    void unsubscribe(std::string_view identifier) {
        channel<MessageT>().unsubscribe(identifier);
    }

    template <typename MessageT>
        requires is_supported_v<MessageT>
    void publish(const MessageT& message) const {
        if (const auto& ch = channel<MessageT>(); !ch.empty()) {
            ch.publish(message);
        }
    }

    template <typename MessageT>
        requires is_supported_v<MessageT>
    [[nodiscard]] std::size_t subscriberCount() const noexcept {
        return channel<MessageT>().subscriberCount();
    }
};
