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
            .now = std::chrono::system_clock::now(),
            .message = "Application bootstrap complete"
        });
              const std::string dsn = bpql::loadCredentials(std::filesystem::path{"conf/bql.conf"})
            .and_then(bpql::validate)
            .transform(bpql::toConnString)
            .value_or(std::string{DatabaseComponent<>::DefaultConnectionString});

        test::runAllFunctionalityTests(bus, dsn);

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



    return 0;
}