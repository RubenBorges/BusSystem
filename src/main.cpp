import std;
import Components;
import DatabaseComponent;
import DataBus;
import EventType;
import Graph;
import UtilitiesQL;
import GraphBridgeComponent; 
import LoggingComponent;
import NotifyingComponent;
import TaskPolicies;
import Pqxx;
import DatabaseCredentials;
import StaticEventBus; // Automatically exports EventChannel, SubscriptionPolicies, StoragePolicies
import HybridBus;
import FunctionalityTest;


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
            .source = "main",
            .message = "Application bootstrap complete",
            .now = std::chrono::system_clock::now()
        });

            const std::string dsn = pgql::loadCredentials(std::filesystem::path{"conf/bql.conf"})
            .and_then(pgql::validate)
            .transform(pgql::toConnString)
            .value_or(std::string{DatabaseComponent<>::DefaultConnectionString});

        test::runAllFunctionalityTests(bus, dsn);

         bus.publish(LogEntry{
            .type = LogEntry::Type::Note,
            .source = "main",
            .message = "Application shutdown sequence starting",
            .now = std::chrono::system_clock::now()
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

    if (notifierStarted) notifier.stop();
    if (loggerStarted) logger.stop();

    return 0;
}