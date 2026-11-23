
# ---------------------------------------------------------
# Source files
# ---------------------------------------------------------
find_package(websocketpp CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(SYSSOURCES
    logserver.cpp
    logserver.hpp
    main.cpp
)

add_executable(DesignedSystem
    ${SYSSOURCES}
)

target_compile_features(DesignedSystem PRIVATE cxx_std_23)
target_include_directories(DesignedSystem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ) 
# 3. Link the targets 
# This automatically handles includes (-I) and library linkage (-l)
target_link_libraries(DesignedSystem PRIVATE
        websocketpp::websocketpp
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)
install(TARGETS DesignedSystem DESTINATION bin)
