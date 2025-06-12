cmake_minimum_required(VERSION 3.10)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(TARGET sse_client_example)
add_executable(${TARGET} sse_client_example.cpp)
target_link_libraries(${TARGET} PRIVATE mcp)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(OPENSSL_FOUND)
    target_link_libraries(${TARGET} PRIVATE ${OPENSSL_LIBRARIES})
endif()

set(TARGET stdio_client_example)
add_executable(${TARGET} stdio_client_example.cpp)
target_link_libraries(${TARGET} PRIVATE mcp)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/include)


set(TARGET server_example)
add_executable(${TARGET} server_example.cpp)
target_link_libraries(${TARGET} PRIVATE mcp)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(OPENSSL_FOUND)
    target_link_libraries(${TARGET} PRIVATE ${OPENSSL_LIBRARIES})
endif()

set(TARGET agent_example)
add_executable(${TARGET} agent_example.cpp)
target_link_libraries(${TARGET} PRIVATE mcp)
target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(OPENSSL_FOUND)
    target_link_libraries(${TARGET} PRIVATE ${OPENSSL_LIBRARIES})
endif()

# Add network subdirectory
add_subdirectory(network)