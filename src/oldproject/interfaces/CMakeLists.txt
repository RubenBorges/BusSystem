add_library(messagebus_interfaces STATIC
    interfaces.cpp
)

target_compile_features(messagebus_interfaces PUBLIC cxx_std_23)

target_include_directories(messagebus_interfaces
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(messagebus_interfaces
    PUBLIC
        messagebus_server
)
