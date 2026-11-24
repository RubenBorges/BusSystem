module;
#include <cstdio>

export module DatabaseComponent;

import std;
export import Pqxx;          // pqxx::result is part of the public API
export import Components;    // also re-exports TaskPolicies, DataBus and EventType
import UtilitiesQL;

#ifndef MAXCOLUMNS__
    #define MAXCOLUMNS__ 64
#endif

inline constexpr std::size_t MaxCols{MAXCOLUMNS__};

export enum class txnState : unsigned char { invalid = '0', valid = '1' };

// Selects the "load connection string from a credentials file" constructor
// (a bare string literal would otherwise be ambiguous with std::string).
export struct ConfigFileTag { explicit ConfigFileTag() = default; };
export inline constexpr ConfigFileTag configFile{};

export template <TaskLaunchPolicy ExecPolicy = DefaultTaskPolicy,
                 ErrorHandlingPolicy ErrPolicy = DefaultErrorPolicy>
class DatabaseComponent : public Component<ExecPolicy, ErrPolicy> {
    using Base = Component<ExecPolicy, ErrPolicy>;

public:
    using QueryResult = std::expected<pqxx::result, std::string>;

    static constexpr std::string_view DefaultConnectionString{
        "dbname=postgres user=postgres hostaddr=127.0.0.1 port=5432"};

    txnState transactionState{txnState::invalid};

    explicit DatabaseComponent(DataBus& bus,
                               std::string connectionString = std::string{DefaultConnectionString})
        : Base(bus), connStr_(std::move(connectionString)) {
        currentTransaction_.reserve(512);
        transactionArgs_.reserve(16);
    }

    DatabaseComponent(DataBus& bus, ConfigFileTag, const std::filesystem::path& file)
        : DatabaseComponent(bus, resolveConnectionString(file)) {}

    // --- Lifecycle ---------------------------------------------------------

    void start() override {
        std::lock_guard lock(connMutex_);
        if (this->started_ && isConnected()) {
            return;
        }
        try {
            if (!isConnected()) {
                conn_.emplace(connStr_);
            }
            this->started_ = true;
        } catch (const std::exception& e) {
            // Deliberately does not print the connection string (may hold credentials).
            std::println(stderr, "Failed to open database connection: {}", e.what());
            conn_.reset();
            this->started_ = false;
        }
    }

    void stop() override {
        std::lock_guard lock(connMutex_);   // waits for a submitted job that is mid-flight
        conn_.reset();
        this->started_ = false;
    }

    [[nodiscard]] bool isConnected() const {
        return conn_.has_value() && conn_->is_open();
    }

    // --- Running work through the execution policy --------------------------
    //
    // `job` is called as job(DatabaseComponent&) under the connection lock, via
    // the component's ExecPolicy: inline for LocalThreadPolicy, on a worker
    // thread for MultiThreadPolicy / AsyncPolicy. It returns the policy's task
    // handle wrapped in std::expected (see TaskExecutor::run), e.g.
    //
    //     auto task = db.submit([](auto& d) { return d.readFromDB("SELECT 1"); });
    //
    // Inside a job use executeWrite / readFromDB / writeToDB-free calls only; the
    // insert *builder* (setTableName, addArgs, addVals, ...) is not thread-safe.
    template <typename Job>
    [[nodiscard]] auto submit(Job&& job) {
        return this->executeTask(
            [this, job = std::forward<Job>(job)]() mutable {
                std::lock_guard lock(connMutex_);
                return job(*this);
            });
    }

    // --- Insert builder (single-threaded) ----------------------------------
    // setTableName -> addArgs (column names) -> buildTransactionFromArgsandVals, then either
    //   addVals(...) + processTransaction()   (values accumulated first), or
    //   writeToDB(...)                        (values passed directly, scaffold reused).
    // The scaffold survives a successful write. Calling addArgs() again after a
    // completed build starts a fresh statement; resetTransactionState() clears everything.

    void setTableName(std::string_view name) { tableName_ = name; }

    void addArgs(std::initializer_list<std::string_view> argList) {
        for (std::string_view arg : argList) addArg(arg);
    }

    template <typename... Ts>
    void addVals(Ts&&... vals) {
        (addVal(std::forward<Ts>(vals)), ...);
    }

    void resetTransactionState() {
        transactionArgs_.clear();
        clearValues();
        currentTransaction_.clear();
        transactionState = txnState::invalid;
    }

    void buildTransactionFromArgsandVals() {
        transactionState = txnState::invalid;
        currentTransaction_.clear();

        if (tableName_.empty()) {
            std::println(stderr, "Table name must be set before building a transaction.");
            return;
        }
        if (!bpql::isSqlIdentifier(tableName_, /*allowSchema=*/true)) {
            std::println(stderr, "Invalid table name '{}'.", tableName_);
            return;
        }
        if (transactionArgs_.empty()) {
            std::println(stderr, "At least one column name is required.");
            return;
        }
        for (const auto& column : transactionArgs_) {
            if (!bpql::isSqlIdentifier(column, /*allowSchema=*/false)) {
                std::println(stderr, "Invalid column name '{}'.", column);
                return;
            }
        }
        if (valCount_ != 0 && valCount_ != transactionArgs_.size()) {
            std::println(stderr, "Argument and value counts must match ({} columns, {} values).",
                         transactionArgs_.size(), valCount_);
            return;
        }

        currentTransaction_ += "INSERT INTO ";
        currentTransaction_ += tableName_;
        currentTransaction_ += " (";
        for (std::size_t i = 0; i < transactionArgs_.size(); ++i) {
            currentTransaction_ += transactionArgs_[i];
            currentTransaction_ += (i + 1 < transactionArgs_.size()) ? ", " : ") VALUES (";
        }
        for (std::size_t i = 0; i < transactionArgs_.size(); ++i) {
            std::format_to(std::back_inserter(currentTransaction_), "${}", i + 1);
            currentTransaction_ += (i + 1 < transactionArgs_.size()) ? ", " : ")";
        }

        transactionState = txnState::valid;
    }

    // --- Execution ---------------------------------------------------------

    // Insert one row using the values accumulated through addVals().
    int processTransaction() {
        if (transactionState != txnState::valid) {
            std::println(stderr, "No valid transaction has been built.");
            publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
            return 1;
        }
        if (valCount_ != transactionArgs_.size()) {
            std::println(stderr, "Expected {} values but {} were added.",
                         transactionArgs_.size(), valCount_);
            publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
            return 1;
        }
        if (!run(DatabaseTx::Mode::Write, currentTransaction_, transactionParams_)) {
            return 1;   // values are kept so the caller may retry
        }
        clearValues();
        return 0;
    }

    // Insert one row using the already-built scaffold and values passed right now.
    template <typename... Args>
    int writeToDB(Args&&... args) {
        if (transactionState != txnState::valid) {
            std::println(stderr, "No valid transaction has been built.");
            publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
            return 1;
        }
        if (sizeof...(Args) != transactionArgs_.size()) {
            std::println(stderr, "Expected {} values but got {}.",
                         transactionArgs_.size(), sizeof...(Args));
            publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
            return 1;
        }
        pqxx::params params;
        (appendParam(params, std::forward<Args>(args)), ...);
        return run(DatabaseTx::Mode::Write, currentTransaction_, params) ? 0 : 1;
    }

    // Arbitrary parameterised SQL in a read/write transaction ($1, $2, ...):
    // DDL, UPDATE/DELETE, INSERT ... RETURNING, etc. Thread-safe.
    template <typename... Args>
    QueryResult executeWrite(std::string_view sql, Args&&... args) {
        pqxx::params params;
        (appendParam(params, std::forward<Args>(args)), ...);
        return run(DatabaseTx::Mode::Write, std::string{sql}, params);
    }

    // Parameterised SELECT in a read-only transaction. Thread-safe.
    template <typename... Args>
    QueryResult readFromDB(std::string_view sql, Args&&... args) {
        pqxx::params params;
        (appendParam(params, std::forward<Args>(args)), ...);
        return run(DatabaseTx::Mode::Read, std::string{sql}, params);
    }

private:
    static std::string resolveConnectionString(const std::filesystem::path& file) {
        using Expected = std::expected<std::string, std::string>;
        return bpql::loadCredentials(file)
            .and_then(bpql::validate)
            .transform(bpql::toConnString)
            .or_else([](const std::string& err) -> Expected {
                std::println(stderr, "Primary config failed ({}), trying fallback...", err);
                return bpql::loadCredentials("fallback_creds.txt")
                    .and_then(bpql::validate)
                    .transform(bpql::toConnString);
            })
            .or_else([](const std::string& err) -> Expected {
                std::println(stderr, "Fallback config failed ({}), using default connection string.", err);
                return std::string{DefaultConnectionString};
            })
            .value_or(std::string{DefaultConnectionString});
    }

    void addArg(std::string_view arg) {
        if (transactionState == txnState::valid) {
            // A new column list after a completed build starts a new statement.
            transactionArgs_.clear();
            currentTransaction_.clear();
            transactionState = txnState::invalid;
        }
        if (transactionArgs_.size() >= MaxCols) {
            throw std::out_of_range("Exceeded MaxCols limit");
        }
        transactionArgs_.emplace_back(arg);   // owned copy: no dangling views
    }

    template <typename T>
    void addVal(T&& val) {
        appendParam(transactionParams_, std::forward<T>(val));
        ++valCount_;
    }

    // pqxx::params keeps views (not copies) of zview/char* arguments, so
    // string-like values are copied into a std::string first.
    template <typename T>
    static void appendParam(pqxx::params& params, T&& value) {
        if constexpr (std::is_convertible_v<T, std::string_view>) {
            params.append(std::string{std::string_view{value}});
        } else {
            params.append(std::forward<T>(value));
        }
    }

    void clearValues() {
        transactionParams_ = pqxx::params{};
        valCount_ = 0;
    }

    // The bus isolates throwing subscribers already; this extra guard keeps the
    // database outcome independent of anything that happens while publishing.
    void publishTxStatus(DatabaseTx::Mode mode, DatabaseTx::Status status, std::uint64_t pgXid) noexcept {
        try {
            this->bus_.publish(DatabaseTx{.mode = mode, .status = status, .pgXid = pgXid});
        } catch (const std::exception& e) {
            std::println(stderr, "Publishing DatabaseTx failed: {}", e.what());
        } catch (...) {
            std::println(stderr, "Publishing DatabaseTx failed with an unknown exception.");
        }
    }

    QueryResult run(DatabaseTx::Mode mode, const std::string& sql, const pqxx::params& params) {
        std::lock_guard lock(connMutex_);

        if (!isConnected()) {
            std::println(stderr, "Database connection is not open.");
            publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
            return std::unexpected{std::string{"Database connection is not open."}};
        }
        if (sql.empty()) {
            std::println(stderr, "No SQL to execute.");
            publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
            return std::unexpected{std::string{"No SQL to execute."}};
        }

        std::uint64_t pgXid = 0;
        pqxx::result result;

        try {
            if (mode == DatabaseTx::Mode::Write) {
                pqxx::work txn(*conn_);

                // Real 64-bit Postgres xid8 (PostgreSQL 13+; use txid_current() on older servers).
                pgXid = txn.query_value<std::uint64_t>("SELECT pg_current_xact_id()::text::bigint");
                publishTxStatus(mode, DatabaseTx::Status::Waiting, pgXid);

                result = txn.exec(sql, params);
                txn.commit();
            } else {
                // Read-only transactions use virtual XIDs (pgXid stays 0).
                publishTxStatus(mode, DatabaseTx::Status::Waiting, 0);

                pqxx::read_transaction txn(*conn_);
                result = txn.exec(sql, params);
                txn.commit();
            }
        } catch (const std::exception& e) {
            std::println(stderr, "Database error (XID {}): {}", pgXid, e.what());
            publishTxStatus(mode, DatabaseTx::Status::Fail, pgXid);
            return std::unexpected{std::string{e.what()}};
        }

        // Outside the try block: the commit succeeded, so this must never report Fail.
        publishTxStatus(mode, DatabaseTx::Status::Success, pgXid);
        return result;
    }

    std::string currentTransaction_;
    std::vector<std::string> transactionArgs_;   // owned column names
    pqxx::params transactionParams_;
    std::size_t valCount_ = 0;

    std::string tableName_;
    std::string connStr_;
    std::optional<pqxx::connection> conn_;
    std::recursive_mutex connMutex_;             // serialises all use of conn_
};
