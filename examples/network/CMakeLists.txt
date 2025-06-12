cmake_minimum_required(VERSION 3.10)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Network Client Example
set(TARGET network_client_example)
add_executable(${TARGET} network_client_example.cpp)
target_link_libraries(${TARGET} PRIVATE mcp)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Simple TCP Server Example (for testing network client)
set(TARGET simple_tcp_server)
add_executable(${TARGET} simple_tcp_server.cpp)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/common)