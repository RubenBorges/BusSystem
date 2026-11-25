cmake_minimum_required(VERSION 3.28)
project(distributedBusSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)
find_package(cppgraphqlgen CONFIG REQUIRED)

set(MODULE_SRCS
    modules/Bus/DataBus.cppm
    modules/Bus/Topic.cppm
    modules/Bus/HybridBus.cppm
    modules/Bus/StaticEventBus.cppm
    modules/Components/Components.cppm
    modules/Components/DatabaseComponent.cppm
    modules/Components/GraphBridgeComponent.cppm
    modules/Components/LoggingComponent.cppm
    modules/Components/NotifyingComponent.cppm
    modules/Database/DatabaseCredentials.cppm
    modules/Database/pqxx.cppm
    modules/Database/UtilitiesQL.cppm
    modules/Database/UtilitiesVK.cppm
    modules/Events/EventType.cppm
    modules/Graph/WireProtocols.cppm
    modules/Graph/Wire.cppm
    modules/Graph/WebSocketConnection.cppm
    modules/Graph/GraphProcess.cppm
    modules/Graph/CentralCoordinatorIDGen.cppm
    modules/Policies/Policies.cppm
    modules/Policies/StoragePolicies.cppm
    modules/Policies/SubscriptionPolicies.cppm
    modules/Policies/TaskPolicies.cppm
    modules/Tests/FunctionalityTest.cppm
    modules/Tests/GraphTest.cppm
)

set(MODULE_IMPL_SRCS
    modules/Graph/GraphProcess.cpp
    modules/Graph/Wire.cpp
    modules/Graph/WebSocketConnection.cpp
    modules/Graph/CentralCoordinatorIDGen.cpp
)

add_library(custom_pqxx SHARED IMPORTED)
set_target_properties(custom_pqxx PROPERTIES
    IMPORTED_LOCATION "/usr/lib/libpqxx.so"
    INTERFACE_INCLUDE_DIRECTORIES "/usr/include"
)
target_link_libraries(custom_pqxx INTERFACE PkgConfig::LIBPQ)

set(CONF_DIR "${CMAKE_SOURCE_DIR}/conf")
set(SCHEMA_GRAPHQL "${CONF_DIR}/schema.graphql")
set(GEN_SCHEMA_DIR "${CMAKE_SOURCE_DIR}/src/schemas")

set(GEN_SCHEMA_PREFIX "distributedBusSystem")
set(GEN_SCHEMA_H "${GEN_SCHEMA_DIR}/${GEN_SCHEMA_PREFIX}Schema.h")

set(GEN_SCHEMA_SOURCES
    "${GEN_SCHEMA_DIR}/${GEN_SCHEMA_PREFIX}Schema.cpp"
    "${GEN_SCHEMA_DIR}/SystemHealthObject.cpp"
    "${GEN_SCHEMA_DIR}/BusEventObject.cpp"
    "${GEN_SCHEMA_DIR}/TemperatureUpdatedObject.cpp"
    "${GEN_SCHEMA_DIR}/GetSystemHealthObject.cpp"
    "${GEN_SCHEMA_DIR}/HealthReportObject.cpp"
    "${GEN_SCHEMA_DIR}/DatabaseTxObject.cpp"
    "${GEN_SCHEMA_DIR}/LogEntryObject.cpp"
    "${GEN_SCHEMA_DIR}/NotifyEventObject.cpp"
    "${GEN_SCHEMA_DIR}/InitFailEventObject.cpp"
    "${GEN_SCHEMA_DIR}/QueryObject.cpp"
    "${GEN_SCHEMA_DIR}/MutationObject.cpp"
)

file(MAKE_DIRECTORY "${GEN_SCHEMA_DIR}")

add_custom_command(
    OUTPUT "${GEN_SCHEMA_H}" ${GEN_SCHEMA_SOURCES}
    COMMAND /usr/local/bin/cppgraphqlgen/schemagen
        --schema "${SCHEMA_GRAPHQL}"
        --prefix "${GEN_SCHEMA_PREFIX}"
        --namespace "generated"
        --header-dir "${GEN_SCHEMA_DIR}"
        --source-dir "${GEN_SCHEMA_DIR}"
    DEPENDS "${SCHEMA_GRAPHQL}"
    COMMENT "Generating distributedBusSystemSchema files"
    VERBATIM
)

add_executable(distributedBusSystem
    main.cpp
    ${MODULE_IMPL_SRCS}
    ${GEN_SCHEMA_H}
    ${GEN_SCHEMA_SOURCES}
)

target_sources(distributedBusSystem
    PRIVATE
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
    FILES ${MODULE_SRCS}
)

target_link_libraries(distributedBusSystem PRIVATE
    Boost::headers
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    custom_pqxx
    cppgraphqlgen::graphqlservice
    cppgraphqlgen::graphqljson
)

target_compile_options(distributedBusSystem PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>
)

target_include_directories(distributedBusSystem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${GEN_SCHEMA_DIR}"
    /usr/local/include
)
