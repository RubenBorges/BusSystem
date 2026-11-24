#include <Bus.hpp>
#include <Components.hpp>
#include <DatabaseComponent.hpp>
#include <eventType.hpp>

#include <cctype>
#include <exception>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <string_view>
#include <variant>

namespace {

bool isIdentifierStart(char value) {
    const auto character = static_cast<unsigned char>(value);
    return std::isalpha(character) || value == '_';
}

bool isIdentifierPart(char value) {
    const auto character = static_cast<unsigned char>(value);
    return std::isalnum(character) || value == '_' || value == '$';
}

bool isSqlIdentifier(std::string_view value, bool allowSchema) {
    bool segmentStart = true;
    for (char character : value) {
        if (allowSchema && character == '.') {
            if (segmentStart) {
                return false;
            }
            segmentStart = true;
        } else if (segmentStart ? isIdentifierStart(character) : isIdentifierPart(character)) {
            segmentStart = false;
        } else {
            return false;
        }
    }
    return !segmentStart;
}

std::string_view statusName(DatabaseTx::Status status) {
    switch (status) {
        case DatabaseTx::Status::Waiting: return "waiting";
        case DatabaseTx::Status::Fail: return "failed";
        case DatabaseTx::Status::Success: return "succeeded";
    }
    return "unknown";
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

    if (!isSqlIdentifier(tableName, true) || !isSqlIdentifier(columnName, false)) {
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
                      << ' ' << statusName(event.status) << " (transaction ";
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

    bool databaseStarted = false;
    int result = 0;
    try {
        database.start();
        databaseStarted = true;
    } catch (const std::exception& error) {
        std::cerr << "[Database] Failed to connect: " << error.what() << '\n';
        result = 1;
    }

    sensor.readSensor();
    dashboard.refresh();

    if (databaseStarted) {
        database.setTableName(tableName);
        database.addArgs({columnName});
        database.addVals(value);
        database.buildTransactionFromArgsandVals();
        if (database.processTransaction() != 0) {
            result = 1;
        }
    }

    database.stop();
    dashboard.stop();
    diagnostics.stop();
    sensor.stop();
    bus.unsubscribe<TemperatureUpdated>("main-temperature-observer");
    bus.unsubscribe<DatabaseTx>("main-database-observer");

    return result;
}
