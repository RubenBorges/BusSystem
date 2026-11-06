
# ---------------------------------------------------------
# Source files
# ---------------------------------------------------------
find_package(websocketpp CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(LOGSOURCES
    logserver.cpp
)

add_executable(logserver
    ${LOGSOURCES}
)

target_compile_features(logserver PRIVATE cxx_std_23)

# 3. Link the targets 
# This automatically handles includes (-I) and library linkage (-l)
target_link_libraries(logserver 
    PRIVATE 
        websocketpp::websocketpp
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)
install(TARGETS logserver DESTINATION bin)
