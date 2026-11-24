find_package(websocketpp CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(libpqxx CONFIG REQUIRED)

set(SYSSOURCES
    main.cpp
modules/Components.cpp
)

add_executable(DesignedSystem
    ${SYSSOURCES}
)

target_compile_features(DesignedSystem PRIVATE cxx_std_26)

# Register module interface units (.cppm / .ixx)
target_sources(DesignedSystem
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
        FILES
            modules/DataBus.cppm
            modules/EventType.cppm
            modules/UtilitiesQL.cppm
            modules/Components.cppm
            modules/DatabaseComponent.cppm
            modules/LoggingComponent.cppm
            modules/SubscriptionPolicies.cppm
            modules/StoragePolicies.cppm
)

target_include_directories(DesignedSystem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Components
    ${CMAKE_CURRENT_SOURCE_DIR}/Policies
    ${CMAKE_CURRENT_SOURCE_DIR}/misc
)

target_link_libraries(DesignedSystem PRIVATE
    websocketpp::websocketpp
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    libpqxx::pqxx
)
