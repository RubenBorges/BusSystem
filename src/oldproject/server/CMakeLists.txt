find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_library(logserver STATIC
    web/server.cpp
    web/client.cpp
)

target_compile_features(logserver PUBLIC cxx_std_23)

target_include_directories(logserver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(logserver
    PUBLIC
        Boost::headers
        Threads::Threads
)


add_executable(logserver
    logserver.cpp
)
