#include <cstddef>
#include <format>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Components.hpp>
#include <pqxx/pqxx>

enum class txnState : unsigned char { invalid = '0', valid = '1' };

class DBComponent : public Component {
    DataBus& bus_;
    bool started_ = false;
    pqxx::connection conn_;
    std::string currentTransaction_; // Holds the SQL query scaffolding
    std::vector<std::string> transactionValues;
    std::vector<std::string> transactionArgs;
    std::string tableName;

public:
    txnState transactionState{txnState::invalid};

    explicit DBComponent(
        DataBus& bus,
        const std::string& connectionString = "dbname=mydb user=myuser password=mypass hostaddr=127.0.0.1 port=5432"
    ) : bus_(bus), conn_(connectionString) {}

    void start() override {}
    void stop() override {}

    // --- Scaffolding Accessors & Builders ---

    std::string& currentTransaction() { return currentTransaction_; }
    const std::string& currentTransaction() const { return currentTransaction_; }

    void setTableName(std::string_view name) { tableName = name; }

    void addArgs(std::initializer_list<std::string> argList) {
        for (const auto& arg : argList) addArg(arg);
    }

    void addVals(std::initializer_list<std::string> valList) {
        for (const auto& val : valList) addVal(val);
    }

    void setTransaction(std::string_view sql) { currentTransaction_ = sql; }
    inline void appendTransaction(std::string_view sqlChunk) { currentTransaction_ += sqlChunk; }
    void clearTransaction() { currentTransaction_.clear(); }

    void resetTransactionState() {
        transactionArgs.clear();
        transactionValues.clear();
        currentTransaction_.clear();
        transactionState = txnState::invalid;
    }

    void buildTransactionFromArgsandVals() {
        transactionState = txnState::invalid;
        currentTransaction_.clear();

        if (transactionArgs.empty() || transactionArgs.size() != transactionValues.size()) {
            std::cerr << "Argument and value counts must be non-empty and equal.\n";
            return;
        }

        // APPEND ARGUMENTS (index-based to handle duplicate names safely)
        appendTransaction(std::format("INSERT INTO {} (", tableName));
        for (std::size_t i = 0; i < transactionArgs.size(); ++i) {
            appendTransaction(transactionArgs[i]);
            appendTransaction(i + 1 < transactionArgs.size() ? ", " : ") ");
        }

        // APPEND VALUES ($1, $2, ...)
        appendTransaction("VALUES (");
        for (std::size_t i = 0; i < transactionValues.size(); ++i) {
            appendTransaction(std::format("${}", i + 1));
            appendTransaction(i + 1 < transactionValues.size() ? ", " : ")");
        }

        transactionState = txnState::valid;
    }

    // --- Execution ---

    int processTransaction() {
        return (transactionState == txnState::valid) ? writeToDB(transactionValues) : 1;
    }

    // Overload 1: Directly accepts a dynamic vector of parameters (no size limit)
    int writeToDB(const std::vector<std::string>& values) {
        pqxx::params params;
        for (const auto& val : values) {
            params.append(val);
        }
        return executeParams(params);
    }

    // Overload 2: Keeps your variadic interface for direct calls like writeToDB(42, 199.95, "completed")
    template <typename... Args>
    int writeToDB(Args&&... args) {
        pqxx::params params{std::forward<Args>(args)...};
        return executeParams(params);
    }

private:
    inline void addVal(std::string val) { transactionValues.push_back(std::move(val)); }
    inline void addArg(std::string arg) { transactionArgs.push_back(std::move(arg)); }

    int executeParams(const pqxx::params& params) {
        if (!conn_.is_open()) {
            std::cerr << "Database connection is not open.\n";
            return 1;
        }

        if (currentTransaction_.empty()) {
            std::cerr << "No transaction SQL scaffolding set.\n";
            return 1;
        }

        try {
            pqxx::work txn(conn_);
            txn.exec(currentTransaction_, params);
            txn.commit();
            resetTransactionState();
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Database error: " << e.what() << "\n";
            return 1;
        }
    }
};
