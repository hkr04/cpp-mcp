cmake_minimum_required(VERSION 3.10)

# Set test project
set(TEST_PROJECT_NAME "mcp_tests")
project(${TEST_PROJECT_NAME})

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Use local Google Test
set(GTEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/googletest)
add_subdirectory(${GTEST_ROOT} googletest-build)

# For older versions of CMake, this option needs to be set
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Include header directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
include_directories(${GTEST_ROOT}/googletest/include)
include_directories(${GTEST_ROOT}/googlemock/include)

# Add test source files
set(TEST_SOURCES
    mcp_test.cpp
)

# Create test executable
add_executable(${TEST_PROJECT_NAME} ${TEST_SOURCES})

# Link directories
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../build/src)

if(WIN32)
    set(MCP_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../build/src/mcp.lib")
else()
    set(MCP_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../build/src/libmcp.a")
endif()

# Link Google Test and MCP library
target_link_libraries(${TEST_PROJECT_NAME} PRIVATE
    GTest::gtest_main
    mcp
    GTest::gmock
)

if (WIN32)
    target_link_libraries(${TEST_PROJECT_NAME} PRIVATE ws2_32 wsock32 crypt32)
endif()

# If OpenSSL is found, link OpenSSL libraries
if(OPENSSL_FOUND)
    target_link_libraries(${TEST_PROJECT_NAME} PRIVATE ${OPENSSL_LIBRARIES})
endif()

if(APPLE)
    set_target_properties(${TEST_PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-no_warn_duplicate_libraries")
endif()

# Enable testing
enable_testing()

# Add test
add_test(
    NAME ${TEST_PROJECT_NAME}
    COMMAND ${TEST_PROJECT_NAME}
)

# Add custom target to run tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS ${TEST_PROJECT_NAME}
) 