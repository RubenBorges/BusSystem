// #include <atomic>
// #include <chrono>
// #include <exception>
// #include <filesystem>
// #include <fstream>
// #include <future>
// #include <iostream>
// #include <string_view>
// #include <system_error>
// #include <thread>
// #include <variant>
import std;
import Components;
import DatabaseComponent;
import DataBus;
import EventType;
import UtilitiesQL;


namespace {

// Ongoing sensor polling loop running on its own thread, publishing to the shared DataBus
void runSensorLoop(SensorComponent& sensor, std::atomic<bool>& running) {
    while (running.load()) {
        sensor.readSensor();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// Background database worker that executes the transaction and fulfills the promise when done
void runDatabaseTask(std::promise<int>&& resultPromise,
                     DatabaseComponent& database,
                     std::string_view tableName,
                     std::string_view columnName,
                     std::string_view value) {
    try {
        database.setTableName(tableName);
        database.addArgs({columnName});
        database.addVals(value);
        database.buildTransactionFromArgsandVals();
        int txResult = database.processTransaction();
        resultPromise.set_value(txResult == 0 ? 0 : 1);
    } catch (...) {
        // Forward any unhandled exception cleanly across threads to fut.get()
        resultPromise.set_exception(std::current_exception());
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << (argc > 0 ? argv[0] : "DesignedSystem")
                  << " <credentials-file> <table> <column> <value>\n"
                  << "The table and column must already exist in the database.\n";
        return 2;
    }

    const std::filesystem::path configFile{argv[1]};
    const std::string_view tableName{argv[2]};
    const std::string_view columnName{argv[3]};
    const std::string_view value{argv[4]};

    if (!bpql::isSqlIdentifier(tableName, true) || !bpql::isSqlIdentifier(columnName, false)) {
        std::cerr << "Table and column must be valid unquoted SQL identifiers.\n";
        return 2;
    }

    std::error_code fileError;
    if (!std::filesystem::is_regular_file(configFile, fileError)) {
        std::cerr << "Credentials path is not a regular file: "
                  << configFile << '\n';
        return 2;
    }
    std::ifstream credentialsCheck(configFile);
    if (!credentialsCheck) {
        std::cerr << "Cannot read credentials file: " << configFile << '\n';
        return 2;
    }

    // Shared, thread-safe DataBus stays on the stack while components use it by reference
    DataBus bus;

    bus.subscribe<TemperatureUpdated>(
        "main-temperature-observer",
        [](const TemperatureUpdated& event) {
            std::cout << "[Main] Temperature observed: " << event.celsius << " C\n";
        });
    bus.subscribe<DatabaseTx>(
        "main-database-observer",
        [](const DatabaseTx& event) {
            std::cout << "[Database] "
                      << (event.mode == DatabaseTx::Mode::Write ? "write" : "read")
                      << ' ' << bpql::statusName(event.status) << " (transaction ";
            std::visit([](const auto& xid) { std::cout << xid; }, event.pgXid);
            std::cout << ")\n";
        });

    SensorComponent sensor(bus);
    DiagnosticsComponent diagnostics(bus);
    DashboardComponent dashboard(bus);
    DatabaseComponent database(bus, configFile);

    sensor.start();
    diagnostics.start();
    dashboard.start();

    // Start ongoing sensor thread
    std::atomic<bool> keepRunning{true};
    std::thread sensorThread(runSensorLoop, std::ref(sensor), std::ref(keepRunning));

    bool databaseStarted = false;
    int result = 0;
    try {
        database.start();
        databaseStarted = true;
    } catch (const std::exception& error) {
        std::cerr << "[Database] Failed to connect: " << error.what() << '\n';
        result = 1;
    }

    dashboard.refresh();

    if (databaseStarted) {
        std::promise<int> dbPromise;
        std::future<int> dbFuture = dbPromise.get_future();

        std::thread dbThread(
            runDatabaseTask,
            std::move(dbPromise),
            std::ref(database),
            tableName,
            columnName,
            value);

        try {
            // Blocks until the database thread calls set_value() or set_exception()
            result = dbFuture.get();
        } catch (const std::exception& error) {
            std::cerr << "[Database] Transaction threw exception: " << error.what() << '\n';
            result = 1;
        }

        dbThread.join();
    }

    // Cleanly stop ongoing worker threads before destroying components or the DataBus
    keepRunning.store(false);
    sensorThread.join();

    database.stop();
    dashboard.stop();
    diagnostics.stop();
    sensor.stop();

    bus.unsubscribe<TemperatureUpdated>("main-temperature-observer");
    bus.unsubscribe<DatabaseTx>("main-database-observer");

    return result;
}
