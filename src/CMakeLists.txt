cmake_minimum_required(VERSION 3.30)

# Enable `import std;` on GCC 15+ / CMake 3.30+ (must be set BEFORE project())
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b5058")

project(DesignedSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    Modules/Components/Components.cpp
)

target_compile_features(DesignedSystem PRIVATE cxx_std_26)

target_sources(DesignedSystem
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Modules
        FILES
            Modules/Databus/DataBus.cppm
            Modules/Events/EventType.cppm
            Modules/Components/Components.cppm
            Modules/Components/DatabaseComponent.cppm
            Modules/Components/LoggingComponent.cppm
            Modules/Components/NotifyingComponent.cppm
            Modules/Policies/SubscriptionPolicies.cppm
            Modules/Policies/StoragePolicies.cppm
            Modules/Policies/Policies.cppm
            Modules/MessageBus/MessageBus.cppm
            Modules/MessageBus/MultiMessageBus.cppm
            Modules/Utility/UtilitiesQL.cppm
            Modules/Utility/pqxx.cppm
)

target_include_directories(DesignedSystem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Components
    ${CMAKE_CURRENT_SOURCE_DIR}/misc
)

target_link_libraries(DesignedSystem PRIVATE
    websocketpp::websocketpp
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    custom_pqxx
)
