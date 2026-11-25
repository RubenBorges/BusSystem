import std;
import Components;
import DatabaseComponent;
import DataBus;
import EventType;
import Graph;
import GraphBridgeComponent; 
import LoggingComponent;
import NotifyingComponent;
import TaskPolicies;
import Pqxx;

import StaticEventBus; // Automatically exports EventChannel, SubscriptionPolicies, StoragePolicies


namespace {

constexpr std::string_view DemoTable{"sensor_readings"};
constexpr std::string_view TxBridgeSubscription{"main-tx-bridge"};
constexpr std::string_view TempCaptureSubscription{"main-temp-capture"};

std::string resolveDsn() {
    if (const char* envDsn = std::getenv("DESIGNED_SYSTEM_DSN"); envDsn && *envDsn) {
        return envDsn;
    }
    return std::string{DatabaseComponent<>::DefaultConnectionString};
}

std::string_view txModeLabel(DatabaseTx::Mode mode) noexcept {
    switch (mode) {
    case DatabaseTx::Mode::Write: return "write";
    case DatabaseTx::Mode::Read:  return "read";
    }
    return "unknown";
}

std::string_view txStatusLabel(DatabaseTx::Status status) noexcept {
    switch (status) {
    case DatabaseTx::Status::Waiting: return "waiting";
    case DatabaseTx::Status::Success: return "success";
    case DatabaseTx::Status::Fail:    return "fail";
    }
    return "unknown";
}

std::string formatPgXid(const std::variant<std::string, std::uint64_t>& xid) {
    return std::visit([](const auto& value) { return std::format("{}", value); }, xid);
}

void demonstrateTaskPolicies() {
    std::println("=== 1. TaskPolicies & TaskExecutor ===");

    TaskExecutor<LocalThreadPolicy, RethrowErrorPolicy> localExec;
    if (auto immediate = localExec.run([] { return 21 * 2; })) {
        std::println("LocalThreadPolicy ready={}, value={}",
                     immediate->is_ready(), immediate->get());
    }

    TaskExecutor<MultiThreadPolicy, RethrowErrorPolicy> mtExec;
    if (auto mtTask = mtExec.run([](std::stop_token token) {
            int ticks = 0;
            while (!token.stop_requested() && ticks < 3) {
                ++ticks;
            }
            return ticks;
        })) {
        std::println("MultiThreadPolicy result={}", mtTask->get());
    }

    TaskExecutor<AsyncPolicy, RethrowErrorPolicy> asyncExec;
    if (auto asyncTask = asyncExec.run([](int a, int b) { return a + b; }, 17, 25)) {
        std::println("AsyncPolicy result={}", asyncTask->get());
    }

    GeneratorPolicy genPolicy;
    auto gen = genPolicy.execute([]() -> std::generator<int> {
        for (int i = 1; i <= 3; ++i) {
            co_yield i * 10;
        }
    }());
    std::print("GeneratorPolicy values:");
    for (int v : gen) {
        std::print(" {}", v);
    }
    std::println("");

    TaskExecutor<LocalThreadPolicy, IgnoreErrorPolicy> resilientExec;
    auto ignored = resilientExec.run([] {
        throw std::runtime_error("absorbed by IgnoreErrorPolicy");
    });
    std::println("IgnoreErrorPolicy has_value={}", ignored.has_value());
}

void demonstrateBusEventsAndHealthRpc(DataBus& bus) {
    std::println("=== 2. DataBus Pub/Sub & Request/Response ===");

    std::atomic<float> latestCelsius{0.0f};
    bus.subscribe<TemperatureUpdated>(
        TempCaptureSubscription,
        [&latestCelsius](const TemperatureUpdated& event) {
            latestCelsius.store(event.celsius, std::memory_order_release);
        });

    bus.subscribe<DatabaseTx>(
        TxBridgeSubscription,
        [&bus](const DatabaseTx& tx) {
            const auto now = std::chrono::system_clock::now();
            const std::string summary = std::format(
                "DB {} {} (xid={})",
                txModeLabel(tx.mode),
                txStatusLabel(tx.status),
                formatPgXid(tx.pgXid));

            bus.publish(LogEntry{
                .type = (tx.status == DatabaseTx::Status::Fail)
                    ? LogEntry::Type::Error
                    : LogEntry::Type::Note,
                .now = now,
                .message = summary
            });

            if (tx.status == DatabaseTx::Status::Fail) {
                bus.publish(NotifyEvent{
                    .type = NotifyEvent::Type::Error,
                    .now = now,
                    .message = summary
                });
            }
        });

    bus.setErrorHandler([](std::string_view subscriber, std::exception_ptr error) {
        try {
            if (error) {
                std::rethrow_exception(error);
            }
        } catch (const std::exception& ex) {
            std::println(std::cerr, "[Bus ErrorHandler] '{}' failed: {}", subscriber, ex.what());
        }
    });

    bus.subscribe<TemperatureUpdated>("flaky-listener", [](const TemperatureUpdated&) {
        throw std::logic_error("simulated listener error");
    });

    SensorComponent<LocalThreadPolicy> localSensor{bus, "sensor-local"};
    SensorComponent<MultiThreadPolicy> threadedSensor{bus, "sensor-mt"};
    SensorComponent<AsyncPolicy> asyncSensor{bus, "sensor-async"};

    DiagnosticsComponent<LocalThreadPolicy> diagnostics{bus, "diag-main"};
    DashboardComponent<LocalThreadPolicy> dashboard{bus, "dash-main"};

    std::vector<std::unique_ptr<IComponent>> managedComponents;
    managedComponents.push_back(std::make_unique<SensorComponent<LocalThreadPolicy>>(bus, "fleet-1"));
    managedComponents.push_back(std::make_unique<SensorComponent<AsyncPolicy>>(bus, "fleet-2"));
    managedComponents.push_back(std::make_unique<DiagnosticsComponent<LocalThreadPolicy>>(bus, "DIII-2"));

    diagnostics.start();
    dashboard.start();
    localSensor.start();
    threadedSensor.start();
    asyncSensor.start();
    for (auto& comp : managedComponents) {
        comp->start();
    }

    localSensor.readSensor();
    std::println("Latest captured temperature after local read: {:.1f} C",
                 latestCelsius.load(std::memory_order_acquire));

    threadedSensor.readSensor();
    asyncSensor.readSensor();
    for (auto& comp : managedComponents) {
        if (auto* s = dynamic_cast<SensorComponent<LocalThreadPolicy>*>(comp.get())) {
            s->readSensor();
        } else if (auto* a = dynamic_cast<SensorComponent<AsyncPolicy>*>(comp.get())) {
            a->readSensor();
        }
    }

    if (bus.hasProvider<GetSystemHealth>()) {
        dashboard.refresh();
        const HealthReport directReport = bus.call<HealthReport>(GetSystemHealth{7});
        std::println("Direct bus.call<HealthReport>: ok={}, status='{}'",
                     directReport.ok, directReport.status);
    }

    bus.unsubscribe<TemperatureUpdated>("flaky-listener");
    bus.unsubscribe<TemperatureUpdated>(TempCaptureSubscription);
    bus.unsubscribe<DatabaseTx>(TxBridgeSubscription);

    for (auto& comp : managedComponents) {
        comp->stop();
    }
    asyncSensor.stop();
    threadedSensor.stop();
    localSensor.stop();
    dashboard.stop();
    diagnostics.stop();
}

void demonstrateGraphNotifications() {
    std::println("=== 3. NotifyingComponent & Graph Transport ===");

    DataBus rootBus;
    DataBus peerBus;

    Process rootProc{100};
    Process peerProc{101};
    rootProc.addChild(&peerProc);
    rootProc.registerChannel(peerProc.identity, PointerChannel<Process>(&peerProc));
    peerProc.registerChannel(rootProc.identity, PointerChannel<Process>(&rootProc));

    NotifyingComponent rootNotifier{rootBus, rootProc};
    NotifyingComponent peerNotifier{peerBus, peerProc};
    rootNotifier.start();
    peerNotifier.start();

    rootBus.publish(NotifyEvent{
        .type = NotifyEvent::Type::Sys,
        .now = std::chrono::system_clock::now(),
        .message = "System startup notification across DataBus + Graph"
    });

    peerNotifier.pollGraph();

    peerNotifier.stop();
    rootNotifier.stop();
}

void demonstrateDatabaseComponent(DataBus& bus, const std::string& dsn) {
    std::println("=== 4. DatabaseComponent Workflows ===");

    DatabaseComponent<LocalThreadPolicy> db{bus, dsn};

    try {
        db.start();
    } catch (const std::exception& ex) {
        std::println(std::cerr, "DatabaseComponent::start() failed: {}", ex.what());
        db.setTableName(DemoTable);
        db.addArgs({"sensor", "celsius"});
        db.addVals("boiler-offline", 55.0);
        db.buildTransactionFromArgsandVals();
        (void)db.processTransaction();
        db.stop();
        return;
    }

    if (!db.isConnected()) {
        std::println(std::cerr, "DatabaseComponent is not connected; skipping live queries.");
        db.stop();
        return;
    }

    if (auto ddl = db.executeWrite(
            "CREATE TABLE IF NOT EXISTS sensor_readings ("
            " id bigserial PRIMARY KEY,"
            " sensor text NOT NULL,"
            " celsius double precision NOT NULL)"); !ddl) {
        std::println(std::cerr, "executeWrite DDL failed: {}", ddl.error());
        db.stop();
        return;
    }

    db.setTableName(DemoTable);
    db.addArgs({"sensor", "celsius"});
    db.addVals("boiler-1", 68.5);
    db.buildTransactionFromArgsandVals();
    if (db.transactionState == txnState::valid) {
        const int rc = db.processTransaction();
        std::println("processTransaction() returned {}", rc);
    }

    db.setTableName(DemoTable);
    db.addArgs({"sensor", "celsius"});
    db.addVals("boiler-2", 71.0);
    db.buildTransactionFromArgsandVals();
    if (db.transactionState == txnState::valid) {
        const int rc = db.writeToDB("boiler-3", 74.2);
        std::println("writeToDB() returned {}", rc);
    }

    if (auto rows = db.readFromDB(
            "SELECT id, sensor, celsius FROM sensor_readings WHERE celsius > $1 ORDER BY id DESC LIMIT 5",
            10.0)) {
        std::println("readFromDB() returned {} row(s)", rows->size());
    } else {
        std::println(std::cerr, "readFromDB() failed: {}", rows.error());
    }

    db.resetTransactionState();
    const int invalidRc = db.processTransaction();
    std::println("processTransaction() on invalid state returned {}", invalidRc);

    db.stop();
}
Message encodeNotifyEvent(const NotifyEvent& event) {
    Message out;
    const auto typeRaw = static_cast<std::uint8_t>(event.type);
    const auto ticks = event.now.time_since_epoch().count();
    const auto len = static_cast<std::uint32_t>(event.message.size());

    out.resize(sizeof(typeRaw) + sizeof(ticks) + sizeof(len) + len);
    std::size_t offset = 0;

    std::memcpy(out.data() + offset, &typeRaw, sizeof(typeRaw));
    offset += sizeof(typeRaw);

    std::memcpy(out.data() + offset, &ticks, sizeof(ticks));
    offset += sizeof(ticks);

    std::memcpy(out.data() + offset, &len, sizeof(len));
    offset += sizeof(len);

    if (len > 0) {
        std::memcpy(out.data() + offset, event.message.data(), len);
    }
    return out;
}

NotifyEvent decodeNotifyEvent(std::span<const std::uint8_t> bytes) {
    using Duration = decltype(NotifyEvent::now)::duration;
    using Rep = Duration::rep;

    constexpr std::size_t headerSize =
        sizeof(std::uint8_t) + sizeof(Rep) + sizeof(std::uint32_t);
    if (bytes.size() < headerSize) {
        throw std::runtime_error("Corrupted NotifyEvent payload");
    }

    std::size_t offset = 0;
    std::uint8_t typeRaw{};
    std::memcpy(&typeRaw, bytes.data() + offset, sizeof(typeRaw));
    offset += sizeof(typeRaw);

    Rep ticks{};
    std::memcpy(&ticks, bytes.data() + offset, sizeof(ticks));
    offset += sizeof(ticks);

    std::uint32_t len{};
    std::memcpy(&len, bytes.data() + offset, sizeof(len));
    offset += sizeof(len);

    if (bytes.size() < offset + len) {
        throw std::runtime_error("Truncated NotifyEvent message string");
    }

    std::string message(
        reinterpret_cast<const char*>(bytes.data() + offset),
        len
    );

    return NotifyEvent{
        .type = static_cast<NotifyEvent::Type>(typeRaw),
        .now = std::chrono::system_clock::time_point{Duration{ticks}},
        .message = std::move(message)
    };
}void demonstrateGraphBridge() {
    std::println("=== 3b. GraphBridgeComponent Cross-Bus Gateway ===");

    DataBus systemABus;
    DataBus systemBBus;

    // Distinct node IDs (200, 201) and server IDs (1, 2) prevent ID/WaveID collisions
    GraphBridgeComponent bridgeA{systemABus, "bridge-A", /*localNodeId=*/200, /*serverId=*/1};
    GraphBridgeComponent bridgeB{systemBBus, "bridge-B", /*localNodeId=*/201, /*serverId=*/2};

    // Link the two gateways over a bidirectional in-memory Channel
    GraphBridgeComponent::bridgeInMemory(bridgeA, bridgeB);

    // Reuse NotifyingComponent's portable big-endian codec on both gateways
    auto decodeNotify = [](std::span<const std::uint8_t> bytes) -> NotifyEvent {
        auto decoded = NotifyingComponent::decodePayload(bytes);
        if (!decoded) {
            throw std::runtime_error("Invalid NotifyEvent magic header in bridged frame");
        }
        return std::move(*decoded);
    };

    bridgeA.bridgeEvent<NotifyEvent>(NotifyingComponent::encode, decodeNotify);
    bridgeB.bridgeEvent<NotifyEvent>(NotifyingComponent::encode, decodeNotify);

    // Activate both gateways so their DataBus subscriptions go live
    bridgeA.start();
    bridgeB.start();

    std::size_t receivedOnB{0};
    std::size_t receivedOnA{0};
    std::size_t totalPublishesOnA{0};
    std::size_t totalPublishesOnB{0};

    systemABus.subscribe<NotifyEvent>("system-a-counter", [&](const NotifyEvent&) {
        ++totalPublishesOnA;
    });
    systemBBus.subscribe<NotifyEvent>("system-b-counter", [&](const NotifyEvent&) {
        ++totalPublishesOnB;
    });

    systemBBus.subscribe<NotifyEvent>("system-b-listener", [&](const NotifyEvent& ev) {
        if (ev.message.starts_with("[From B]")) {
            return;
        }
        ++receivedOnB;
        std::println("[System B] Received bridged NotifyEvent: '{}'", ev.message);

        if (ev.message == "[From A] Ping") {
            systemBBus.publish(NotifyEvent{
                .type = NotifyEvent::Type::Note,
                .now = std::chrono::system_clock::now(),
                .message = "[From B] Pong reply"
            });
        }
    });

    systemABus.subscribe<NotifyEvent>("system-a-listener", [&](const NotifyEvent& ev) {
        if (ev.message.starts_with("[From A]")) {
            return;
        }
        ++receivedOnA;
        std::println("[System A] Received bridged NotifyEvent: '{}'", ev.message);
    });

    systemABus.publish(NotifyEvent{
        .type = NotifyEvent::Type::Sys,
        .now = std::chrono::system_clock::now(),
        .message = "[From A] Ping"
    });

    systemBBus.publish(NotifyEvent{
        .type = NotifyEvent::Type::Note,
        .now = std::chrono::system_clock::now(),
        .message = "[From B] Telemetry heartbeat"
    });

    systemABus.publish(NotifyEvent{
        .type = NotifyEvent::Type::Sys,
        .now = std::chrono::system_clock::now(),
        .message = "[From A] Configuration sync complete"
    });

    bridgeA.poll();
    bridgeB.poll();

    std::println("Bidirectional check: B received={} (bus total={}), A received={} (bus total={})",
                 receivedOnB, totalPublishesOnB, receivedOnA, totalPublishesOnA);

    systemABus.unsubscribe<NotifyEvent>("system-a-listener");
    systemBBus.unsubscribe<NotifyEvent>("system-b-listener");
    systemABus.unsubscribe<NotifyEvent>("system-a-counter");
    systemBBus.unsubscribe<NotifyEvent>("system-b-counter");
    bridgeB.unbridgeEvent<NotifyEvent>();
    bridgeA.unbridgeEvent<NotifyEvent>();
    bridgeB.stop();
    bridgeA.stop();
}

// Example custom policy: limits a single MessageBus channel to at most MaxSubs subscribers
template <std::size_t MaxSubs>
struct BoundedSubscriptionPolicy : SubscriptionPolicy {
    std::size_t active{0};

    void subscribe(std::string_view identifier) {
        if (active >= MaxSubs) {
            throw std::length_error(
                std::format("Channel full (max {}): rejected '{}'", MaxSubs, identifier));
        }
        ++active;
    }

    void unsubscribe(std::string_view) noexcept {
        if (active > 0) {
            --active;
        }
    }
};
void demonstratePolicyEventBuses(DataBus& mainBus) {
    std::println("=== 5. Policy-Based EventChannel & StaticEventBus ===");

    // 1. Single-type EventChannel with BoundedSubscriptionPolicy<1> and move-only state
    EventChannel<TemperatureUpdated, VectorPolicy, BoundedSubscriptionPolicy<1>> exclusiveSensorChannel;

    auto runningPeak = std::make_unique<float>(-273.15f);
    exclusiveSensorChannel.subscribe("peak-tracker", [peak = std::move(runningPeak)](const TemperatureUpdated& ev) {
        if (ev.celsius > *peak) {
            *peak = ev.celsius;
        }
        std::println("[EventChannel] Sample {:.1f} C (session peak={:.1f} C)", ev.celsius, *peak);
    });

    try {
        exclusiveSensorChannel.subscribe("intruder", [](const TemperatureUpdated&) {});
    } catch (const std::length_error& ex) {
        std::println("[EventChannel Policy] Rejected 2nd subscriber: {}", ex.what());
    }

    exclusiveSensorChannel.publish(TemperatureUpdated{.celsius = 21.5f});
    exclusiveSensorChannel.publish(TemperatureUpdated{.celsius = 24.8f});
    exclusiveSensorChannel.unsubscribe("peak-tracker");

    // 2. Compile-time StaticEventBus for lock-free local telemetry aggregation
    StaticEventBus<TemperatureUpdated, NotifyEvent> fastLocalBus;
    std::println("Size of StaticEventBus<2 types>: {} bytes", sizeof(fastLocalBus));

    float sumCelsius = 0.0f;
    std::size_t sampleCount = 0;

    fastLocalBus.subscribe<TemperatureUpdated>("local-aggregator", [&](const TemperatureUpdated& ev) {
        sumCelsius += ev.celsius;
        ++sampleCount;
    });

    fastLocalBus.subscribe<NotifyEvent>("local-forwarder", [&](const NotifyEvent& ev) {
        // Forward aggregated milestone onto the thread-safe global DataBus
        mainBus.publish(ev);
    });

    for (float reading : {20.0f, 22.0f, 24.0f}) {
        fastLocalBus.publish(TemperatureUpdated{.celsius = reading});
    }

    if (sampleCount > 0) {
        const float avg = sumCelsius / static_cast<float>(sampleCount);
        fastLocalBus.publish(NotifyEvent{
            .type = NotifyEvent::Type::Note,
            .now = std::chrono::system_clock::now(),
            .message = std::format("StaticEventBus aggregated {} samples (avg={:.1f} C)", sampleCount, avg)
        });
    }

    fastLocalBus.unsubscribe<TemperatureUpdated>("local-aggregator");
    fastLocalBus.unsubscribe<NotifyEvent>("local-forwarder");
}


} // namespace

int main() {
    DataBus bus;
    std::println("Size of Databus object: {}", sizeof(bus));
    
    LoggingComponent logger{bus, std::filesystem::path{"designed-system.log"}};
    NotifyingComponent notifier{bus};
    
    std::println("Size of LoggingComponent object: {}", sizeof(logger));
    std::println("Size of NotifyingComponent object: {}", sizeof(notifier));
    
    bool loggerStarted = false;
    bool notifierStarted = false;

    try {
        logger.start();
        loggerStarted = true;

        notifier.start();
        notifierStarted = true;

        bus.publish(LogEntry{
            .type = LogEntry::Type::Sys,
            .now = std::chrono::system_clock::now(),
            .message = "Application bootstrap complete"
        });

        demonstrateTaskPolicies();
        demonstrateBusEventsAndHealthRpc(bus);
        demonstrateGraphNotifications();
        demonstrateDatabaseComponent(bus, resolveDsn());

        bus.publish(LogEntry{
            .type = LogEntry::Type::Note,
            .now = std::chrono::system_clock::now(),
            .message = "Application shutdown sequence starting"
        });
    } catch (const std::exception& ex) {
        std::println(std::cerr, "[main] Unhandled exception: {}", ex.what());
        if (notifierStarted) {
            notifier.stop();
        }
        if (loggerStarted) {
            logger.stop();
        }
        return 1;
    }

    if (notifierStarted) {
        notifier.stop();
    }
    if (loggerStarted) {
        logger.stop();
    }

    std::println("Beginning GraphNodeComponent Demo!");
    demonstrateGraphBridge();

    std::println("Beginning EventChannelBus Demo!");
    demonstratePolicyEventBuses(bus); // <-- Add this call

    return 0;
}