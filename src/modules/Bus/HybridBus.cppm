export module HybridBus;

import std;
export import DataBus;
export import StaticEventBus;

export template <typename... HotEvents>
class HybridBus {
private:
    template <typename Event>
    static constexpr bool is_hot_v = (std::same_as<Event, HotEvents> || ...);

    StaticEventBus<HotEvents...> fastBus_;
    DataBus& dynamicBus_;

public:
    explicit HybridBus(DataBus& dynamicBus) noexcept : dynamicBus_(dynamicBus) {}

    // --- Unified Pub/Sub (Compile-Time Routed) ----------------------------

    template <typename Event, typename Fn>
    void subscribe(std::string_view identifier, Fn&& callback) {
        if constexpr (is_hot_v<Event>) {
            fastBus_.template subscribe<Event>(identifier, std::forward<Fn>(callback));
        } else {
            dynamicBus_.subscribe<Event>(identifier, std::forward<Fn>(callback));
        }
    }

    template <typename Event>
    void unsubscribe(std::string_view identifier) {
        if constexpr (is_hot_v<Event>) {
            fastBus_.template unsubscribe<Event>(identifier);
        } else {
            dynamicBus_.unsubscribe<Event>(identifier);
        }
    }

    template <typename Event>
    void publish(const Event& event) const {
        if constexpr (is_hot_v<Event>) {
            fastBus_.template publish<Event>(event);
        } else {
            dynamicBus_.publish<Event>(event);
        }
    }

    // --- Selective Promotion (Hot Fast-Path -> Global DataBus) ------------
    // Lets global components (LoggingComponent, DatabaseComponent, GraphBridge)
    // tap into a hot event stream without slowing down local fast-path listeners.
    template <typename Event>
        requires is_hot_v<Event>
    void promote(std::string_view bridgeId = "hybrid-promoter") {
        fastBus_.template subscribe<Event>(bridgeId, [this](const Event& event) {
            dynamicBus_.publish(event);
        });
    }

    template <typename Event>
        requires is_hot_v<Event>
    void demote(std::string_view bridgeId = "hybrid-promoter") {
        fastBus_.template unsubscribe<Event>(bridgeId);
    }

    // --- Pass-Through RPCs & Direct Access --------------------------------

    template <typename Request, typename Response>
    void provide(std::function<Response(const Request&)> handler) {
        dynamicBus_.provide<Request, Response>(std::move(handler));
    }

    template <typename Response, typename Request>
    Response call(const Request& request) const {
        return dynamicBus_.call<Response>(request);
    }

    [[nodiscard]] DataBus& dynamicBus() noexcept { return dynamicBus_; }
    [[nodiscard]] StaticEventBus<HotEvents...>& fastBus() noexcept { return fastBus_; }
};
