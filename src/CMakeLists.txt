find_package(Boost CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)
find_package(cppgraphqlgen CONFIG REQUIRED)

set(PROJECT_NAME distributedBusSystem)

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

# 1. Executable + standard .cpp files (including generated schema .cpp files)
add_executable(distributedBusSystem
    main.cpp
    modules/Graph/Graph.cpp
    modules/Graph/CentralCoordinatorIDGen.cpp
    modules/Graph/WebSocketConnection.cpp
    modules/Graph/Wire.cpp
    "${GEN_SCHEMA_H}"
    ${GEN_SCHEMA_SOURCES}
)

# 2. C++20/26 module interface (.cppm) files ONLY
target_sources(distributedBusSystem
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
        FILES
            modules/Bus/DataBus.cppm
            modules/Bus/EventChannel.cppm
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
            modules/Graph/Graph.cppm
            modules/Policies/Policies.cppm
            modules/Policies/StoragePolicies.cppm
            modules/Policies/SubscriptionPolicies.cppm
            modules/Policies/TaskPolicies.cppm
            modules/Tests/FunctionalityTest.cppm
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

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        modules/Graph/Graph.cppm
        modules/Graph/Graph.cpp
        modules/Graph/CentralCoordinatorIDGen.cpp
        modules/Graph/WebSocketConnection.cpp
        modules/Graph/Wire.cpp
        modules/Database/UtilitiesVK.cppm
        ${GEN_SCHEMA_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-fno-gnu-unique"
    )
endif()

target_include_directories(distributedBusSystem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${GEN_SCHEMA_DIR}"
)
