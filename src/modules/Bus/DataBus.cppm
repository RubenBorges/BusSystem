/*
 *  DataBus: Dynamic Hub-and-Spoke (Runtime Type Erasure)
 *
 *  Central messaging hub: components publish events and subscribe to them, or
 *  provide/call request-response handlers. The bus knows nothing about storage,
 *  logging, or threading policies; it only routes messages.
 *
 *  Delivery & Safety Guarantees (fixed version — public API unchanged):
 *    * publish() and call() never hold the bus lock while running callbacks.
 *    * Throwing subscribers are isolated and routed to the error handler.
 *    * unsubscribe() and unprovide() block until any in-flight execution of
 *      that specific handler finishes, preventing use-after-free on teardown.
 *      Self-unsubscribe / self-unprovide from inside the handler itself is
 *      safe: it marks the handler inactive and returns immediately; the
 *      current callback still runs to completion and is not called again.
 *    * Callbacks of ONE subscriber never run concurrently with each other
 *      on different threads. Nested re-entry on the *same* thread is
 *      still allowed (as in the original — e.g. a subscriber publishing
 *      a follow-up event it also handles), but it is bounded by the
 *      recursion-depth limits below, so it cannot loop forever.
 *    * Recursion depth is bounded for both publish() and call() to prevent
 *      infinite event loops (kMaxPublishDepth / kMaxCallDepth, both 32).
 *      publish() depth is now counted for every nested publish, not only
 *      for publishes that happen to have subscribers.
 *    * A call() that snapshot a provider before that provider was replaced
 *      or removed completes with the handler it started with, instead of
 *      spuriously failing because active was re-read after the swap.
 *    * ScopedSubscription guards no longer dangle if the DataBus is
 *      destroyed first: subscription state is held in a shared State
 *      object, so a guard can still unsubscribe safely after bus teardown.
 *
 *  Known limitation, unchanged by design (documented, not silently fixed):
 *    * Unsubscribing / unproviding a *different* handler that is currently
 *      executing on *another* thread, from inside a callback, is a circular
 *      wait if that other callback is in turn waiting for this one. That is
 *      a deadlock in the caller's logic, as with any synchronous teardown.
 *      Do not do that; use the RAII guard (safe for self) or unsubscribe
 *      from outside the callback.
 *
 *  Changes vs. the original, in short:
 *    1. Shared State object (mtx / subscribers / providers / errorHandler)
 *       owned by shared_ptr, captured by ScopedSubscription lambdas.
 *    2. Thread-local executing-depth counts for subscribers and
 *       providers, used to skip waiting on self. Nested delivery on
 *       the same thread is *not* skipped — that would silently drop
 *       follow-up events a subscriber publishes to itself.
 *    3. publish() depth guard moved before the snapshot; call() gets the
 *       same bounded depth guard (kMaxCallDepth).
 *    4. call() captures active-at-snapshot under the bus lock and checks
 *       that captured value, not the live atomic, after acquiring the
 *       provider lock.
 *    5. New additive, non-breaking drain() helper for teardown.
 */

export module DataBus;

import std;

export class DataBus {
private:
    struct Subscriber {
        std::string identifier;
        std::function<void(const void*)> callback;
        std::atomic<bool> active{true};
        std::recursive_mutex running;
    };

    struct Provider {
        std::type_index responseType;
        std::function<std::any(const std::any&)> handler;
        std::atomic<bool> active{true};
        std::recursive_mutex running;

        Provider(std::type_index respType, std::function<std::any(const std::any&)> fn)
            : responseType(respType), handler(std::move(fn)) {}
    };

    using SubscriberList = std::vector<std::shared_ptr<Subscriber>>;

    // Shared state: kept alive by the bus, by in-flight snapshots, and by
    // ScopedSubscription guards, so teardown of the DataBus itself cannot
    // leave a guard or an in-flight publish/call with a dangling bus.
    struct State {
        mutable std::shared_mutex mtx;
        std::unordered_map<std::type_index, std::shared_ptr<const SubscriberList>> subscribers;
        std::unordered_map<std::type_index, std::shared_ptr<Provider>> providers;
        std::function<void(std::string_view, std::exception_ptr)> errorHandler;
    };

    // Thread-local execution tracking. These are per-thread, so a check
    // here only ever detects re-entry / self-teardown on *this* thread;
    // cross-thread serialisation is still done by `running`.
    static inline thread_local std::size_t publishDepth_ = 0;
    static inline thread_local std::size_t callDepth_ = 0;
    // Nesting depth per handler on this thread (0 = not executing here).
    // A count, not a set, so nested re-entry on the same thread is
    // tracked correctly and still allowed.
    static inline thread_local std::unordered_map<const Subscriber*, std::size_t> executingSubscribers_;
    static inline thread_local std::unordered_map<const Provider*, std::size_t> executingProviders_;

    struct SubscriberExecutionGuard {
        Subscriber& sub;
        explicit SubscriberExecutionGuard(Subscriber& s) : sub(s) {
            ++executingSubscribers_[&sub];
        }
        ~SubscriberExecutionGuard() {
            auto it = executingSubscribers_.find(&sub);
            if (it != executingSubscribers_.end() && --it->second == 0) {
                executingSubscribers_.erase(it);
            }
        }
        SubscriberExecutionGuard(const SubscriberExecutionGuard&) = delete;
        SubscriberExecutionGuard& operator=(const SubscriberExecutionGuard&) = delete;
    };

    struct ProviderExecutionGuard {
        Provider& prov;
        explicit ProviderExecutionGuard(Provider& p) : prov(p) {
            ++executingProviders_[&prov];
        }
        ~ProviderExecutionGuard() {
            auto it = executingProviders_.find(&prov);
            if (it != executingProviders_.end() && --it->second == 0) {
                executingProviders_.erase(it);
            }
        }
        ProviderExecutionGuard(const ProviderExecutionGuard&) = delete;
        ProviderExecutionGuard& operator=(const ProviderExecutionGuard&) = delete;
    };

    struct DepthGuard {
        std::size_t& depth;
        explicit DepthGuard(std::size_t& d) : depth(d) { ++depth; }
        ~DepthGuard() { --depth; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
    };

    // Wait for a handler to finish, unless it is the handler currently
    // executing on this thread — in that case waiting would be a no-op
    // at best (recursive mutex) and the current callback will finish
    // on its own after this call returns.
    static void waitForSubscriber(const std::shared_ptr<Subscriber>& sub) {
        if (executingSubscribers_.contains(sub.get())) {
            return;
        }
        std::lock_guard wait{sub->running};
    }

    static void waitForProvider(const std::shared_ptr<Provider>& prov) {
        if (executingProviders_.contains(prov.get())) {
            return;
        }
        std::lock_guard wait{prov->running};
    }

public:
    using ErrorHandler =
        std::function<void(std::string_view subscriber, std::exception_ptr error)>;

    static constexpr std::size_t kMaxPublishDepth = 32;
    static constexpr std::size_t kMaxCallDepth = 32;

    // --- RAII Subscription Guard ------------------------------------------
    class [[nodiscard]] ScopedSubscription {
    public:
        ScopedSubscription() = default;
        ScopedSubscription(std::function<void()> unsubscribeFn)
            : unsubscribeFn_(std::move(unsubscribeFn)) {}

        ~ScopedSubscription() { reset(); }

        ScopedSubscription(ScopedSubscription&& other) noexcept
            : unsubscribeFn_(std::exchange(other.unsubscribeFn_, nullptr)) {}

        ScopedSubscription& operator=(ScopedSubscription&& other) noexcept {
            if (this != &other) {
                reset();
                unsubscribeFn_ = std::exchange(other.unsubscribeFn_, nullptr);
            }
            return *this;
        }

        ScopedSubscription(const ScopedSubscription&) = delete;
        ScopedSubscription& operator=(const ScopedSubscription&) = delete;

        void reset() {
            if (auto fn = std::exchange(unsubscribeFn_, nullptr)) {
                fn();
            }
        }

    private:
        std::function<void()> unsubscribeFn_;
    };

    DataBus() = default;
    DataBus(const DataBus&) = delete;
    DataBus& operator=(const DataBus&) = delete;

    // --- Events -----------------------------------------------------------

    // Registers a callback and returns an RAII guard (can be ignored if managing manually).
    template <typename Event>
    ScopedSubscription subscribe(std::string_view identifier,
                                 std::function<void(const Event&)> callback) {
        auto subscriber = std::make_shared<Subscriber>();
        subscriber->identifier = identifier;
        subscriber->callback = [callback = std::move(callback)](const void* data) {
            callback(*static_cast<const Event*>(data));
        };

        const auto eventType = std::type_index{typeid(Event)};
        auto state = state_;
        {
            std::unique_lock lock(state->mtx);
            auto& slot = state->subscribers[eventType];
            auto next = slot ? std::make_shared<SubscriberList>(*slot)
                             : std::make_shared<SubscriberList>();
            next->push_back(subscriber);
            slot = std::move(next);
        }

        // Capture the shared State, not `this`: the guard stays valid even
        // if the DataBus object itself is destroyed first.
        return ScopedSubscription([state, eventType, weakSub = std::weak_ptr(subscriber)]() {
            unsubscribeInstance(state, eventType, weakSub);
        });
    }

    // Removes every subscription of `Event` registered under `identifier`.
    template <typename Event>
    void unsubscribe(std::string_view identifier) {
        SubscriberList removed;
        auto state = state_;
        {
            std::unique_lock lock(state->mtx);
            auto found = state->subscribers.find(std::type_index{typeid(Event)});
            if (found == state->subscribers.end() || !found->second) {
                return;
            }

            auto next = std::make_shared<SubscriberList>();
            next->reserve(found->second->size());

            for (const auto& sub : *found->second) {
                if (sub->identifier == identifier) {
                    sub->active.store(false);
                    removed.push_back(sub);
                } else {
                    next->push_back(sub);
                }
            }

            if (removed.empty()) {
                return;
            }
            found->second = std::move(next);
        }

        for (const auto& subscriber : removed) {
            waitForSubscriber(subscriber);
        }
    }

    // Removes all subscriptions across ALL event types for `identifier`.
    void unsubscribeAll(std::string_view identifier) {
        SubscriberList removed;
        auto state = state_;
        {
            std::unique_lock lock(state->mtx);
            for (auto& [type, slot] : state->subscribers) {
                if (!slot || slot->empty()) continue;

                bool modified = false;
                auto next = std::make_shared<SubscriberList>();
                next->reserve(slot->size());

                for (const auto& sub : *slot) {
                    if (sub->identifier == identifier) {
                        sub->active.store(false);
                        removed.push_back(sub);
                        modified = true;
                    } else {
                        next->push_back(sub);
                    }
                }

                if (modified) {
                    slot = std::move(next);
                }
            }
        }

        for (const auto& subscriber : removed) {
            waitForSubscriber(subscriber);
        }
    }

    template <typename Event>
    void publish(const Event& event) const {
        if (publishDepth_ >= kMaxPublishDepth) {
            throw std::runtime_error("DataBus: maximum event recursion depth exceeded");
        }
        // Count this publish *before* the snapshot, so nested publishes
        // count even when they have no subscribers.
        DepthGuard depthGuard{publishDepth_};

        auto state = state_;
        std::shared_ptr<const SubscriberList> snapshot;
        ErrorHandler errHandlerSnapshot;
        {
            std::shared_lock lock(state->mtx);
            auto found = state->subscribers.find(std::type_index{typeid(Event)});
            if (found == state->subscribers.end() || !found->second || found->second->empty()) {
                return;
            }
            snapshot = found->second;
            errHandlerSnapshot = state->errorHandler;
        }

        for (const auto& subscriber : *snapshot) {
            deliver(*subscriber, &event, errHandlerSnapshot);
        }
    }

    template <typename Event>
    [[nodiscard]] std::size_t subscriberCount() const {
        auto state = state_;
        std::shared_lock lock(state->mtx);
        auto found = state->subscribers.find(std::type_index{typeid(Event)});
        return (found == state->subscribers.end() || !found->second) ? 0 : found->second->size();
    }

    // --- Request / Response -----------------------------------------------

    template <typename Request, typename Response>
    void provide(std::function<Response(const Request&)> handler, bool overwrite = true) {
        auto provider = std::make_shared<Provider>(
            std::type_index{typeid(Response)},
            [handler = std::move(handler)](const std::any& request) -> std::any {
                return handler(std::any_cast<const Request&>(request));
            }
        );

        auto state = state_;
        std::shared_ptr<Provider> previous;
        {
            std::unique_lock lock(state->mtx);
            const auto reqType = std::type_index{typeid(Request)};
            if (!overwrite && state->providers.contains(reqType)) {
                throw std::runtime_error("DataBus: provider already registered for request type");
            }
            if (auto it = state->providers.find(reqType); it != state->providers.end()) {
                previous = std::move(it->second);
                previous->active.store(false);
            }
            state->providers[reqType] = std::move(provider);
        }

        // Wait for any in-flight call on the replaced provider to finish.
        // Calls that snapshot the old provider before the swap complete
        // with it (see call()); new calls already go to the new provider.
        if (previous) {
            waitForProvider(previous);
        }
    }

    template <typename Request>
    void unprovide() {
        auto state = state_;
        std::shared_ptr<Provider> removed;
        {
            std::unique_lock lock(state->mtx);
            auto it = state->providers.find(std::type_index{typeid(Request)});
            if (it == state->providers.end()) {
                return;
            }
            removed = std::move(it->second);
            removed->active.store(false);
            state->providers.erase(it);
        }

        // Wait (without holding bus lock) for any active call() to finish
        waitForProvider(removed);
    }

    template <typename Request>
    [[nodiscard]] bool hasProvider() const {
        auto state = state_;
        std::shared_lock lock(state->mtx);
        return state->providers.contains(std::type_index{typeid(Request)});
    }

    template <typename Response, typename Request>
    Response call(const Request& request) const {
        if (callDepth_ >= kMaxCallDepth) {
            throw std::runtime_error("DataBus: maximum call recursion depth exceeded");
        }
        DepthGuard depthGuard{callDepth_};

        auto state = state_;
        std::shared_ptr<Provider> provider;
        bool activeAtSnapshot = false;
        {
            std::shared_lock lock(state->mtx);
            auto found = state->providers.find(std::type_index{typeid(Request)});
            if (found == state->providers.end() || !found->second) {
                throw std::runtime_error("DataBus: no component registered to handle this call");
            }
            provider = found->second;
            // Capture this under the bus lock. Re-reading `active` after
            // acquiring `running` would spuriously fail a call that
            // started before a provide()/unprovide() swap.
            activeAtSnapshot = provider->active.load();
        }

        if (provider->responseType != std::type_index{typeid(Response)}) {
            throw std::logic_error("DataBus: requested Response type does not match provider");
        }

        std::lock_guard guard(provider->running);
        if (!activeAtSnapshot) {
            throw std::runtime_error("DataBus: provider was unregistered before call executed");
        }

        ProviderExecutionGuard exec(*provider);
        return std::any_cast<Response>(provider->handler(request));
    }

    // --- Diagnostics ------------------------------------------------------

    void setErrorHandler(ErrorHandler handler) {
        auto state = state_;
        std::unique_lock lock(state->mtx);
        state->errorHandler = std::move(handler);
    }

    // New, additive only: wait for all currently in-flight subscriber
    // callbacks and provider calls to finish. Call this after stopping
    // new publishes/calls, before tearing down components that handlers
    // depend on. It is best-effort if new work starts concurrently:
    // handlers that start after the snapshot are not waited for.
    // Self-executing handlers are skipped, as in unsubscribe()/unprovide().
    void drain() const {
        auto state = state_;
        SubscriberList subs;
        std::vector<std::shared_ptr<Provider>> provs;
        {
            std::shared_lock lock(state->mtx);
            for (const auto& [type, slot] : state->subscribers) {
                if (slot) subs.insert(subs.end(), slot->begin(), slot->end());
            }
            for (const auto& [type, prov] : state->providers) {
                if (prov) provs.push_back(prov);
            }
        }
        for (const auto& sub : subs) {
            waitForSubscriber(sub);
        }
        for (const auto& prov : provs) {
            waitForProvider(prov);
        }
    }

private:
    static void unsubscribeInstance(std::shared_ptr<State> state,
                                    std::type_index eventType,
                                    std::weak_ptr<Subscriber> weakSub) {
        auto target = weakSub.lock();
        if (!target) return;

        target->active.store(false);
        {
            std::unique_lock lock(state->mtx);
            auto found = state->subscribers.find(eventType);
            if (found != state->subscribers.end() && found->second) {
                auto next = std::make_shared<SubscriberList>();
                next->reserve(found->second->size());
                for (const auto& sub : *found->second) {
                    if (sub != target) {
                        next->push_back(sub);
                    }
                }
                found->second = std::move(next);
            }
        }

        waitForSubscriber(target);
    }

    static void deliver(Subscriber& subscriber,
                        const void* payload,
                        const ErrorHandler& errorHandler) {
        // Note: nested re-entry on the same thread is allowed here
        // (recursive_mutex + publish-depth bound). Skipping it would
        // silently drop follow-up events a subscriber publishes to
        // itself. Cross-thread concurrency is still prevented by
        // `running`: another thread blocks until this delivery ends.
        std::lock_guard guard(subscriber.running);
        if (!subscriber.active.load()) {
            return;
        }
        SubscriberExecutionGuard exec(subscriber);
        try {
            subscriber.callback(payload);
        } catch (...) {
            reportError(subscriber.identifier, std::current_exception(), errorHandler);
        }
    }

    static void reportError(std::string_view identifier,
                            std::exception_ptr error,
                            const ErrorHandler& errorHandler) {
        if (errorHandler) {
            try {
                errorHandler(identifier, error);
                return;
            } catch (...) {
                // Fall through to default stderr logging if custom handler throws
            }
        }

        try {
            if (error) std::rethrow_exception(error);
        } catch (const std::exception& e) {
            std::println(stderr, "[DataBus] subscriber '{}' threw: {}", identifier, e.what());
        } catch (...) {
            std::println(stderr, "[DataBus] subscriber '{}' threw an unknown exception", identifier);
        }
    }

    std::shared_ptr<State> state_ = std::make_shared<State>();
};