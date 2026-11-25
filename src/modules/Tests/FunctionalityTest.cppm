
module;

#include <sys/types.h>

export module FunctionalityTest;

import std;
import Components;
import DatabaseComponent;
import DataBus;
import EventType;
import GraphProcess;
import WireProtocols;
import GraphBridgeComponent;
import HybridBus;
import LoggingComponent;
import NotifyingComponent;
import Pqxx;
import StaticEventBus;
import TaskPolicies;

using Process = GraphProcess;
using namespace protocol;

namespace test {

constexpr std::string_view DemoTable{"sensor_readings"};
constexpr std::string_view TxBridgeSubscription{"main-tx-bridge"};
constexpr std::string_view TempCaptureSubscription{"main-temp-capture"};

std::string resolveDsn() {
  if (const char *envDsn = std::getenv("DESIGNED_SYSTEM_DSN");
      envDsn && *envDsn) {
    return envDsn;
  }
  return std::string{DatabaseComponent<>::DefaultConnectionString};
}

std::string_view txModeLabel(DatabaseTx::Mode mode) noexcept {
  switch (mode) {
  case DatabaseTx::Mode::Write:
    return "write";
  case DatabaseTx::Mode::Read:
    return "read";
  }
  return "unknown";
}

std::string_view txStatusLabel(DatabaseTx::Status status) noexcept {
  switch (status) {
  case DatabaseTx::Status::Waiting:
    return "waiting";
  case DatabaseTx::Status::Success:
    return "success";
  case DatabaseTx::Status::Fail:
    return "fail";
  }
  return "unknown";
}

std::string formatPgXid(const std::variant<std::string, std::uint64_t> &xid) {
  return std::visit([](const auto &value) { return std::format("{}", value); },
                    xid);
}

void demonstrateTaskPolicies() {
  std::println("=== 1. TaskPolicies & TaskExecutor ===");

  TaskExecutor<LocalThreadPolicy, RethrowErrorPolicy> localExec;
  if (auto immediate = localExec.run([] { return 21 * 2; })) {
    std::println("LocalThreadPolicy ready={}, value={}", immediate->is_ready(),
                 immediate->get());
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
  if (auto asyncTask =
          asyncExec.run([](int a, int b) { return a + b; }, 17, 25)) {
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
  auto ignored = resilientExec.run(
      [] { throw std::runtime_error("absorbed by IgnoreErrorPolicy"); });
  std::println("IgnoreErrorPolicy has_value={}", ignored.has_value());
}

void demonstrateBusEventsAndHealthRpc(DataBus &bus) {
  std::println("=== 2. DataBus Pub/Sub & Request/Response ===");

  std::atomic<float> latestCelsius{0.0f};
bus.subscribe<TemperatureUpdated>(
      TempCaptureSubscription,
      [&latestCelsius](const TemperatureUpdated &event) {
        latestCelsius.store(event.celsius, std::memory_order_release);
      });

bus.subscribe<DatabaseTx>(TxBridgeSubscription, [&bus](const DatabaseTx &tx) {
    const auto now = std::chrono::system_clock::now();
    const std::string summary =
        std::format("DB {} {} (xid={})", txModeLabel(tx.mode),
                    txStatusLabel(tx.status), formatPgXid(tx.pgXid));

    bus.publish(LogEntry{.type = (tx.status == DatabaseTx::Status::Fail)
                                     ? LogEntry::Type::Error
                                     : LogEntry::Type::Note,
                         .source = TxBridgeSubscription,
                         .message = summary,
                         .now = now});

    if (tx.status == DatabaseTx::Status::Fail) {
      bus.publish(NotifyEvent{
          .type = NotifyEvent::Type::Error, .now = now, .message = summary});
    }
  });

  bus.setErrorHandler(
      [](std::string_view subscriber, std::exception_ptr error) {
        try {
          if (error) {
            std::rethrow_exception(error);
          }
        } catch (const std::exception &ex) {
          std::println(std::cerr, "[Bus ErrorHandler] '{}' failed: {}",
                       subscriber, ex.what());
        }
      });

bus.subscribe<TemperatureUpdated>(
      "flaky-listener", [](const TemperatureUpdated &) {
        throw std::logic_error("simulated listener error");
      });

  SensorComponent<LocalThreadPolicy> localSensor{bus, "sensor-local"};
  SensorComponent<MultiThreadPolicy> threadedSensor{bus, "sensor-mt"};
  SensorComponent<AsyncPolicy> asyncSensor{bus, "sensor-async"};

  DiagnosticsComponent<LocalThreadPolicy> diagnostics{bus, "diag-main"};
  DashboardComponent<LocalThreadPolicy> dashboard{bus, "dash-main"};

  std::vector<std::unique_ptr<IComponent>> managedComponents;
  managedComponents.push_back(
      std::make_unique<SensorComponent<LocalThreadPolicy>>(bus, "fleet-1"));
  managedComponents.push_back(
      std::make_unique<SensorComponent<AsyncPolicy>>(bus, "fleet-2"));
  managedComponents.push_back(
      std::make_unique<DiagnosticsComponent<LocalThreadPolicy>>(bus, "DIII-2"));

  diagnostics.start();
  dashboard.start();
  localSensor.start();
  threadedSensor.start();
  asyncSensor.start();
  for (auto &comp : managedComponents) {
    comp->start();
  }

  localSensor.readSensor();
  std::println("Latest captured temperature after local read: {:.1f} C",
               latestCelsius.load(std::memory_order_acquire));

  threadedSensor.readSensor();
  asyncSensor.readSensor();
  for (auto &comp : managedComponents) {
    if (auto *s =
            dynamic_cast<SensorComponent<LocalThreadPolicy> *>(comp.get())) {
      s->readSensor();
    } else if (auto *a =
                   dynamic_cast<SensorComponent<AsyncPolicy> *>(comp.get())) {
      a->readSensor();
    }
  }

  if (bus.hasProvider<GetSystemHealth>()) {
    dashboard.refresh();
    const HealthReport directReport =
        bus.call<HealthReport>(GetSystemHealth{7});
    std::println("Direct bus.call<HealthReport>: ok={}, status='{}'",
                 directReport.ok, directReport.status);
  }

  bus.unsubscribe<TemperatureUpdated>("flaky-listener");
  bus.unsubscribe<TemperatureUpdated>(TempCaptureSubscription);
  bus.unsubscribe<DatabaseTx>(TxBridgeSubscription);

  for (auto &comp : managedComponents) {
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
  rootProc.registerChannel(peerProc.identity,
                           PointerChannel<Process>(&peerProc));
  peerProc.registerChannel(rootProc.identity,
                           PointerChannel<Process>(&rootProc));

  NotifyingComponent rootNotifier{rootBus, rootProc};
  NotifyingComponent peerNotifier{peerBus, peerProc};
  rootNotifier.start();
  peerNotifier.start();

  rootBus.publish(NotifyEvent{
      .type = NotifyEvent::Type::Sys,
      .now = std::chrono::system_clock::now(),
      .message = "System startup notification across DataBus + Graph"});

  peerNotifier.pollGraph();

  peerNotifier.stop();
  rootNotifier.stop();
}

void demonstrateDatabaseComponent(DataBus &bus, const std::string &dsn) {
  std::println("=== 4. DatabaseComponent Workflows ===");

  DatabaseComponent<LocalThreadPolicy> db{bus, dsn};

  try {
    db.start();
  } catch (const std::exception &ex) {
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
    std::println(std::cerr,
                 "DatabaseComponent is not connected; skipping live queries.");
    db.stop();
    return;
  }

  if (auto ddl = db.executeWrite("CREATE TABLE IF NOT EXISTS sensor_readings ("
                                 " id bigserial PRIMARY KEY,"
                                 " sensor text NOT NULL,"
                                 " celsius double precision NOT NULL)");
      !ddl) {
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
        std::println("readFromDB() returned {} row(s):", rows->size());

        for (const auto& row : *rows) {
            const auto id      = row["id"].as<std::int64_t>();
            const auto sensor  = row["sensor"].as<std::string_view>();
            const auto celsius = row["celsius"].as<float>();

            std::println("  -> id={}, sensor='{}', celsius={:.1f} C", id, sensor, celsius);
        }
    } else {
        std::println(std::cerr, "readFromDB() failed: {}", rows.error());
    }


  db.resetTransactionState();
  const int invalidRc = db.processTransaction();
  std::println("processTransaction() on invalid state returned {}", invalidRc);

  db.stop();
}
Message encodeNotifyEvent(const NotifyEvent &event) {
  Message out;
  const auto typeRaw = static_cast<std::uint8_t>(event.type);
  const auto ticks = event.now.time_since_epoch().count();
  const auto len = static_cast<std::uint32_t>(event.message.size());

  out.resize(sizeof(typeRaw) + sizeof(ticks) + sizeof(len) + len);
  ssize_t offset = 0;

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

  constexpr ssize_t headerSize =
      sizeof(std::uint8_t) + sizeof(Rep) + sizeof(std::uint32_t);
  if (bytes.size() < headerSize) {
    throw std::runtime_error("Corrupted NotifyEvent payload");
  }

  ssize_t offset = 0;
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

  std::string message(reinterpret_cast<const char *>(bytes.data() + offset),
                      len);

  return NotifyEvent{.type = static_cast<NotifyEvent::Type>(typeRaw),
                     .now =
                         std::chrono::system_clock::time_point{Duration{ticks}},
                     .message = std::move(message)};
}

void demonstrateGraphBridge() {
  std::println("=== 3b. GraphBridgeComponent Cross-Bus Gateway ===");

  DataBus systemABus;
  DataBus systemBBus;

  // Distinct node IDs (200, 201) and server IDs (1, 2) prevent ID/WaveID
  // collisions
  GraphBridgeComponent bridgeA{systemABus, "bridge-A", /*localNodeId=*/200,
                               /*serverId=*/1};
  GraphBridgeComponent bridgeB{systemBBus, "bridge-B", /*localNodeId=*/201,
                               /*serverId=*/2};

  // Link the two gateways over a bidirectional in-memory Channel
  GraphBridgeComponent::bridgeInMemory(bridgeA, bridgeB,
                                       BridgeTopology::PeerToPeer);

  // Reuse NotifyingComponent's portable big-endian codec on both gateways
  auto decodeNotify = [](std::span<const std::uint8_t> bytes) -> NotifyEvent {
    auto decoded = NotifyingComponent::decodePayload(bytes);
    if (!decoded) {
      throw std::runtime_error(
          "Invalid NotifyEvent magic header in bridged frame");
    }
    return std::move(*decoded);
  };

  bridgeA.bridgeEvent<NotifyEvent>(NotifyingComponent::encode, decodeNotify);
  bridgeB.bridgeEvent<NotifyEvent>(NotifyingComponent::encode, decodeNotify);

  // Activate both gateways so their DataBus subscriptions go live
  bridgeA.start();
  bridgeB.start();

  ssize_t receivedOnB{0};
  ssize_t receivedOnA{0};
  ssize_t totalPublishesOnA{0};
  ssize_t totalPublishesOnB{0};

   systemABus.subscribe<NotifyEvent>(
      "system-a-counter", [&](const NotifyEvent &) { ++totalPublishesOnA; });
systemBBus.subscribe<NotifyEvent>(
      "system-b-counter", [&](const NotifyEvent &) { ++totalPublishesOnB; });

  systemBBus.subscribe<NotifyEvent>(
      "system-b-listener", [&](const NotifyEvent &ev) {
        if (ev.message.starts_with("[From B]")) {
          return;
        }
        ++receivedOnB;
        std::println("[System B] Received bridged NotifyEvent: '{}'",
                     ev.message);

        if (ev.message == "[From A] Ping") {
          systemBBus.publish(
              NotifyEvent{.type = NotifyEvent::Type::Note,
                          .now = std::chrono::system_clock::now(),
                          .message = "[From B] Pong reply"});
        }
      });

 systemABus.subscribe<NotifyEvent>(
      "system-a-listener", [&](const NotifyEvent &ev) {
        if (ev.message.starts_with("[From A]")) {
          return;
        }
        ++receivedOnA;
        std::println("[System A] Received bridged NotifyEvent: '{}'",
                     ev.message);
      });

  systemABus.publish(NotifyEvent{.type = NotifyEvent::Type::Sys,
                                 .now = std::chrono::system_clock::now(),
                                 .message = "[From A] Ping"});

  systemBBus.publish(NotifyEvent{.type = NotifyEvent::Type::Note,
                                 .now = std::chrono::system_clock::now(),
                                 .message = "[From B] Telemetry heartbeat"});

  systemABus.publish(
      NotifyEvent{.type = NotifyEvent::Type::Sys,
                  .now = std::chrono::system_clock::now(),
                  .message = "[From A] Configuration sync complete"});

  bridgeA.poll();
  bridgeB.poll();

  std::println("Bidirectional check: B received={} (bus total={}), A "
               "received={} (bus total={})",
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

// Example custom policy: limits a single MessageBus channel to at most MaxSubs
// subscribers
template <ssize_t MaxSubs>
struct BoundedSubscriptionPolicy : SubscriptionPolicy {
  ssize_t active{0};

  void subscribe(std::string_view identifier) {
    if (active >= MaxSubs) {
      throw std::length_error(std::format(
          "Channel full (max {}): rejected '{}'", MaxSubs, identifier));
    }
    ++active;
  }

  void unsubscribe(std::string_view) noexcept {
    if (active > 0) {
      --active;
    }
  }
};
void demonstratePolicyEventBuses(DataBus &mainBus) {
  std::println("=== 5. Policy-Based Topic & StaticEventBus ===");

  // 1. Single-type Topic with BoundedSubscriptionPolicy<1> and move-only
  // state
  Topic<TemperatureUpdated, VectorPolicy, BoundedSubscriptionPolicy<1>>
      exclusiveSensorChannel;

  auto runningPeak = std::make_unique<float>(-273.15f);
  exclusiveSensorChannel.subscribe(
      "peak-tracker",
      [peak = std::move(runningPeak)](const TemperatureUpdated &ev) {
        if (ev.celsius > *peak) {
          *peak = ev.celsius;
        }
        std::println("[Topic] Sample {:.1f} C (session peak={:.1f} C)",
                     ev.celsius, *peak);
      });

  try {
    exclusiveSensorChannel.subscribe("intruder",
                                     [](const TemperatureUpdated &) {});
  } catch (const std::length_error &ex) {
    std::println("[Topic Policy] Rejected 2nd subscriber: {}",
                 ex.what());
  }

  exclusiveSensorChannel.publish(TemperatureUpdated{.celsius = 21.5f});
  exclusiveSensorChannel.publish(TemperatureUpdated{.celsius = 24.8f});
  exclusiveSensorChannel.unsubscribe("peak-tracker");

  // 2. Compile-time StaticEventBus for lock-free local telemetry aggregation
  StaticEventBus<TemperatureUpdated, NotifyEvent> fastLocalBus;
  std::println("Size of StaticEventBus<2 types>: {} bytes",
               sizeof(fastLocalBus));

  float sumCelsius = 0.0f;
  ssize_t sampleCount = 0;

  fastLocalBus.subscribe<TemperatureUpdated>("local-aggregator",
                                             [&](const TemperatureUpdated &ev) {
                                               sumCelsius += ev.celsius;
                                               ++sampleCount;
                                             });

  fastLocalBus.subscribe<NotifyEvent>("local-forwarder",
                                      [&](const NotifyEvent &ev) {
                                        // Forward aggregated milestone onto the
                                        // thread-safe global DataBus
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
        .message =
            std::format("StaticEventBus aggregated {} samples (avg={:.1f} C)",
                        sampleCount, avg)});
  }

  fastLocalBus.unsubscribe<TemperatureUpdated>("local-aggregator");
  fastLocalBus.unsubscribe<NotifyEvent>("local-forwarder");
}
void demonstrateHybridBus(DataBus &mainBus) {
  std::println("=== 6. HybridBus (Multi-Tier Pipeline, Bridge Promotion & "
               "Benchmark) ===");

  // Both TemperatureUpdated and LogEntry take the lock-free StaticEventBus
  // fast-path; NotifyEvent and GetSystemHealth RPCs automatically route through
  // mainBus (DataBus).
  HybridBus<TemperatureUpdated, LogEntry> hybrid{mainBus};

  // --- Stage 1: Sliding-Window Anomaly Detector on the Hot Path ----------
  constexpr ssize_t WindowSize = 4;
  std::array<float, WindowSize> window{};
  ssize_t sampleCount = 0;
  ssize_t spikeCount = 0;
  ssize_t suppressedDebugLogs = 0;
  bool incidentMode = false;

  auto movingAverage = [&]() -> float {
    const ssize_t count = std::min(sampleCount, WindowSize);
    if (count == 0)
      return 0.0f;
    float sum = 0.0f;
    for (ssize_t i = 0; i < count; ++i)
      sum += window[i];
    return sum / static_cast<float>(count);
  };

  // Hot-path subscriber #1: filters high-frequency LogEntry noise locally
  hybrid.subscribe<LogEntry>("hot-log-filter", [&](const LogEntry &entry) {
    if (entry.type == LogEntry::Type::Note && !incidentMode) {
      ++suppressedDebugLogs; // Absorbed on fast-path without touching mainBus
                             // or disk
      return;
    }
    // Escalate critical/incident logs onto mainBus for LoggingComponent
    mainBus.publish(entry);
  });

  // Hot-path subscriber #2: computes sliding-window average & detects thermal
  // spikes
  hybrid.subscribe<TemperatureUpdated>(
      "hot-thermal-analyzer", [&](const TemperatureUpdated &ev) {
        const float prevAvg = movingAverage();
        window[sampleCount % WindowSize] = ev.celsius;
        ++sampleCount;

         // Emit hot-path trace log (zero DataBus locking)
        const auto msg = std::format("Sample #{}: {:.1f} C (avg={:.1f} C)",
                                     sampleCount, ev.celsius, movingAverage());
        hybrid.publish(LogEntry{
            .type = LogEntry::Type::Note,
            .source = "hot-thermal-analyzer",
            .message = msg,
            .now = std::chrono::system_clock::now()});

        // Detect sudden >15 C jump or >85 C threshold breach
        if ((sampleCount > 1 && (ev.celsius - prevAvg) > 15.0f) ||
            ev.celsius >= 85.0f) {
          ++spikeCount;
          // NotifyEvent is NOT in HotEvents..., so it routes directly to
          // mainBus!
          hybrid.publish(NotifyEvent{
              .type = NotifyEvent::Type::Warning,
              .now = std::chrono::system_clock::now(),
              .message =
                  std::format("Thermal spike #{}: {:.1f} C (baseline {:.1f} C)",
                              spikeCount, ev.celsius, prevAvg)});
        }
      });

  // --- Stage 2: Wire a Remote Monitoring Bus via GraphBridgeComponent -----
  DataBus remoteOpsBus;
  GraphBridgeComponent localGateway{mainBus, "hybrid-local-gw",
                                    /*localNodeId=*/300, /*serverId=*/3};
  GraphBridgeComponent remoteGateway{remoteOpsBus, "hybrid-remote-gw",
                                     /*localNodeId=*/301, /*serverId=*/4};
  GraphBridgeComponent::bridgeInMemory(localGateway, remoteGateway,
                                       BridgeTopology::PeerToPeer);

  // TemperatureUpdated is trivially copyable, so default binary codec works out
  // of the box
  localGateway.bridgeEvent<TemperatureUpdated>();
  remoteGateway.bridgeEvent<TemperatureUpdated>();
  localGateway.start();
  remoteGateway.start();

  ssize_t mainBusTempCount = 0;
  ssize_t remoteOpsTempCount = 0;

  mainBus.subscribe<TemperatureUpdated>(
      "main-temp-auditor",
      [&](const TemperatureUpdated &) { ++mainBusTempCount; });
  remoteOpsBus.subscribe<TemperatureUpdated>(
      "remote-ops-watcher", [&](const TemperatureUpdated &ev) {
        ++remoteOpsTempCount;
        std::println("[Remote Ops Bus] Live promoted telemetry: {:.1f} C",
                     ev.celsius);
      });

  // Normal operation: 4 steady readings stay strictly on the local fast-path
  for (float reading : {40.0f, 41.5f, 40.5f, 42.0f}) {
    hybrid.publish(TemperatureUpdated{.celsius = reading});
  }
  std::println("[Steady State] samples={}, spikes={}, suppressedLogs={}, "
               "mainBusTemps={}, remoteTemps={}",
               sampleCount, spikeCount, suppressedDebugLogs, mainBusTempCount,
               remoteOpsTempCount);

  // Thermal spike triggers incident mode -> dynamically promote TemperatureUpdated to mainBus & remoteOpsBus!
  incidentMode = true;
  hybrid.promote<TemperatureUpdated>("incident-promoter");

  for (float reading : {68.0f, 89.5f, 76.0f}) {
    hybrid.publish(TemperatureUpdated{.celsius = reading});
  }
  localGateway.poll();
  remoteGateway.poll();

  std::println("[Incident Mode] samples={}, spikes={}, suppressedLogs={}, "
               "mainBusTemps={}, remoteTemps={}",
               sampleCount, spikeCount, suppressedDebugLogs, mainBusTempCount,
               remoteOpsTempCount);

  // --- Stage 3: Live RPC Query Against Hot-Path Telemetry State ----------
  hybrid.provide<GetSystemHealth, HealthReport>(
      [&](const GetSystemHealth &req) {
        return HealthReport{
            .ok = (spikeCount <= 2),
            .status = std::format(
                "Subsystem {} | avg={:.1f} C | spikes={} | promoted={}",
                req.subsystemId, movingAverage(), spikeCount, incidentMode)};
      });

  const auto report = hybrid.call<HealthReport>(GetSystemHealth{42});
  std::println("[Hybrid RPC] ok={}, status='{}'", report.ok, report.status);

  // Demote back to normal mode (hot path detaches from mainBus & GraphBridge)
  hybrid.demote<TemperatureUpdated>("incident-promoter");
  incidentMode = false;
  hybrid.publish(TemperatureUpdated{.celsius = 43.0f});
  std::println("[Post-Demote]  samples={}, mainBusTemps={} (unchanged), "
               "remoteTemps={} (unchanged)",
               sampleCount, mainBusTempCount, remoteOpsTempCount);

  remoteOpsBus.unsubscribe<TemperatureUpdated>("remote-ops-watcher");
  remoteGateway.unbridgeEvent<TemperatureUpdated>();
  localGateway.unbridgeEvent<TemperatureUpdated>();
  remoteGateway.stop();
  localGateway.stop();

  // --- Stage 4: 10,000-Event Fast-Path vs. Dynamic DataBus Micro-Benchmark -
  hybrid.unsubscribe<TemperatureUpdated>("hot-thermal-analyzer");
  mainBus.unsubscribe<TemperatureUpdated>("main-temp-auditor");

  volatile float sink = 0.0f;
  hybrid.subscribe<TemperatureUpdated>(
      "bench-hot", [&](const TemperatureUpdated &ev) { sink = ev.celsius; });
  mainBus.subscribe<TemperatureUpdated>(
      "bench-dynamic",
      [&](const TemperatureUpdated &ev) { sink = ev.celsius; });

  constexpr ssize_t Iterations = 10'000;

  const auto t0 = std::chrono::steady_clock::now();
  for (ssize_t i = 0; i < Iterations; ++i) {
    hybrid.publish(TemperatureUpdated{.celsius = 50.0f});
  }
  const auto t1 = std::chrono::steady_clock::now();

  for (ssize_t i = 0; i < Iterations; ++i) {
    mainBus.publish(TemperatureUpdated{.celsius = 50.0f});
  }
  const auto t2 = std::chrono::steady_clock::now();

  const auto hotNs =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  const auto dynNs =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
  std::println("[Benchmark {} msgs] HybridBus fast-path: {} us ({:.1f} ns/msg) "
               "| DataBus: {} us ({:.1f} ns/msg)",
               Iterations, hotNs / 1000,
               static_cast<double>(hotNs) / Iterations, dynNs / 1000,
               static_cast<double>(dynNs) / Iterations);

  // Clean teardown in reverse dependency order
  hybrid.unsubscribe<TemperatureUpdated>("bench-hot");
  hybrid.unsubscribe<LogEntry>("hot-log-filter");
  mainBus.unsubscribe<TemperatureUpdated>("bench-dynamic");
  mainBus.unprovide<GetSystemHealth>();
}

export void runAllFunctionalityTests(DataBus &bus, const std::string &dsn) {
  demonstrateTaskPolicies();
  demonstrateBusEventsAndHealthRpc(bus);
  demonstrateGraphNotifications();
  demonstrateDatabaseComponent(bus, dsn);
  demonstrateGraphBridge();
  demonstratePolicyEventBuses(bus);
  demonstrateHybridBus(bus);
}
} // namespace test
