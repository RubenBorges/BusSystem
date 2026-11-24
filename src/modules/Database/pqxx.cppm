module;

#include <pqxx/pqxx>

export module Pqxx;

// Only include this line if you enable CMAKE_CXX_MODULE_STD in CMake 3.30+:
// import std;

export namespace pqxx {
    using ::pqxx::connection;
    using ::pqxx::work;
    using ::pqxx::read_transaction;
    using ::pqxx::nontransaction;
    using ::pqxx::transaction_base;
    using ::pqxx::result;
    using ::pqxx::row;
    using ::pqxx::field;
    using ::pqxx::params;
    using ::pqxx::zview;
    using ::pqxx::sql_error;
    using ::pqxx::broken_connection;
    using ::pqxx::conversion_error;
    using ::pqxx::unexpected_rows;
}
