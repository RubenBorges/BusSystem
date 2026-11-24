find_package(websocketpp CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)

add_library(custom_pqxx SHARED IMPORTED)
set_target_properties(custom_pqxx PROPERTIES
    IMPORTED_LOCATION "/usr/lib/libpqxx.so"
    INTERFACE_INCLUDE_DIRECTORIES "/usr/include"
)
target_link_libraries(custom_pqxx INTERFACE PkgConfig::LIBPQ)

add_executable(DesignedSystem
    main.cpp
    modules/Graph/CentralCoordinatorIDGen.cpp
    modules/Graph/Graph.cpp
    modules/Graph/WebSocketConnection.cpp
    modules/Graph/Wire.cpp
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        modules/Graph/Graph.cppm
        modules/Graph/WebSocketConnection.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-gnu-unique"
    )
endif()

target_sources(DesignedSystem
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
        FILES
            modules/Databus/DataBus.cppm
            modules/Events/EventType.cppm
            modules/Components/Components.cppm
            modules/Components/DatabaseComponent.cppm
            modules/Components/LoggingComponent.cppm
            modules/Components/NotifyingComponent.cppm
            modules/Policies/SubscriptionPolicies.cppm
            modules/Policies/StoragePolicies.cppm
            modules/Policies/TaskPolicies.cppm
            modules/Policies/Policies.cppm
            modules/MessageBus/MessageBus.cppm
            modules/MessageBus/MultiMessageBus.cppm
            modules/UtilitiesQL.cppm
            modules/pqxx.cppm
            modules/Graph/Graph.cppm
)


target_link_libraries(DesignedSystem PRIVATE
websocketpp::websocketpp
Boost::system
OpenSSL::SSL
OpenSSL::Crypto
Threads::Threads
custom_pqxx
)

# target_include_directories(DesignedSystem PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}/include
# )