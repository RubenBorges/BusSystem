find_package(websocketpp CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(libpqxx CONFIG REQUIRED)

set(SYSSOURCES
    main.cpp
    Components/modules/Components.cpp
)

add_executable(DesignedSystem
    ${SYSSOURCES}
)

target_compile_features(DesignedSystem PRIVATE cxx_std_26)

# Register module interface units (.cppm / .ixx)
target_sources(DesignedSystem
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Components/modules
        FILES
            Components/modules/DataBus.cppm
            Components/modules/EventType.cppm
            Components/modules/UtilitiesQL.cppm
            Components/modules/Components.cppm
            Components/modules/DatabaseComponent.cppm
            Components/modules/LoggingComponent.cppm
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
