#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <Components.hpp>
#include <pqxx/pqxx>
#include <UtilitiesQL.hpp>

#ifndef MAXCOLUMNS__
    #define MAXCOLUMNS__ 64
#endif

inline constexpr std::size_t MaxCols{MAXCOLUMNS__};

enum class txnState : unsigned char { invalid = '0', valid = '1' };

class DBComponent : public Component {
//    static constexpr std::size_t MaxCols = MAXCOLS__; // Practical cap (PG hard protocol limit is 65,535)

    DataBus& bus_;
    bool started_ = false;
    std::string currentTransaction_;
    
    // Lightweight fixed-capacity views for column names (zero heap allocations)
    std::array<std::string_view, MaxCols> transactionArgs_{};
    std::size_t argCount_ = 0;

    // Build pqxx::params directly instead of buffering in std::vector<std::string>
    pqxx::params transactionParams_;
    std::size_t valCount_ = 0;

    std::string tableName;
    std::string connStr_;
    std::optional<pqxx::connection> conn_;

public:
    txnState transactionState{txnState::invalid};

    explicit DBComponent(
        DataBus& bus,
        std::string connectionString = "dbname=mydb user=myuser password=mypass hostaddr=127.0.0.1 port=5432"
    ) : bus_(bus), connStr_(std::move(connectionString)) {
        currentTransaction_.reserve(512);
    }

    void start() override {
        if (!conn_ || !conn_->is_open()) {
            conn_.emplace(connStr_);
        }
        started_ = true;
    }

    void stop() override {
        conn_.reset();
        started_ = false;
    }

    // --- Lightweight Builders ---

    void setTableName(std::string_view name) { tableName = name; }

    void addArgs(std::initializer_list<std::string_view> argList) {
        for (std::string_view arg : argList) addArg(arg);
    }

    // Accepts any type pqxx supports (int, double, string_view, etc.) without forcing std::to_string!
    template <typename... Ts>
    void addVals(Ts&&... vals) {
        (addVal(std::forward<Ts>(vals)), ...);
    }

    void resetTransactionState() {
        argCount_ = 0;
        valCount_ = 0;
        transactionParams_ = pqxx::params{}; // Resets internal buffer
        currentTransaction_.clear();         // Keeps allocated string capacity
        transactionState = txnState::invalid;
    }

    void buildTransactionFromArgsandVals() {
        transactionState = txnState::invalid;
        currentTransaction_.clear();

        if (argCount_ == 0 || argCount_ != valCount_) {
            std::cerr << "Argument and value counts must be non-empty and equal.\n";
            return;
        }

        currentTransaction_ += "INSERT INTO ";
        currentTransaction_ += tableName;
        currentTransaction_ += " (";

        for (std::size_t i = 0; i < argCount_; ++i) {
            currentTransaction_ += transactionArgs_[i];
            currentTransaction_ += (i + 1 < argCount_) ? ", " : ") VALUES (";
        }

        for (std::size_t i = 0; i < valCount_; ++i) {
            std::format_to(std::back_inserter(currentTransaction_), "${}", i + 1);
            currentTransaction_ += (i + 1 < valCount_) ? ", " : ")";
        }

        transactionState = txnState::valid;
    }

    // --- Execution ---

    int processTransaction() {
        return (transactionState == txnState::valid) ? executeParams(transactionParams_) : 1;
    }

    template <typename... Args>
    int writeToDB(Args&&... args) {
        pqxx::params params{std::forward<Args>(args)...};
        return executeParams(params);
    }

private:
    void addArg(std::string_view arg) {
        if (argCount_ >= MaxCols) {
            throw std::out_of_range("Exceeded MaxCols limit");
        }
        transactionArgs_[argCount_++] = arg;
    }

    template <typename T>
    void addVal(T&& val) {
        transactionParams_.append(std::forward<T>(val));
        ++valCount_;
    }

    int executeParams(const pqxx::params& params) {
        if (!conn_ || !conn_->is_open()) {
            std::cerr << "Database connection is not open.\n";
            return 1;
        }

        if (currentTransaction_.empty()) {
            std::cerr << "No transaction SQL scaffolding set.\n";
            return 1;
        }

        try {
            pqxx::work txn(*conn_);
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

