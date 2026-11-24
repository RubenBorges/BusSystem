
set(GRAPH_MODULE_HEADERS
    CentralCoordinatorIDGen.hpp
    Process.hpp
    WebSocketChannel.hpp
    WebSocketConnection.hpp
    Wire.hpp
)

set_source_files_properties(
    ${GRAPH_MODULE_HEADERS}
    PROPERTIES
        LANGUAGE CXX
        CXX_SCAN_FOR_MODULES ON
)

add_library(Graph
    Process.cpp
    Wire.cpp
    WebSocketConnection.cpp
    CentralCoordinatorIDGen.cpp
    GraphDemo.cpp
)

target_sources(Graph
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            Graph.cppm
            CentralCoordinatorIDGen.cppm
            Process.cppm
            WebSocketChannel.cppm
            WebSocketConnection.cppm
            Wire.cppm
            ${GRAPH_MODULE_HEADERS}
)

target_include_directories(Graph SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
target_compile_definitions(Graph PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
target_link_libraries(Graph PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# ---------------------------------------------------------
# Link dependencies
# ---------------------------------------------------------



install(TARGETS Graph
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)