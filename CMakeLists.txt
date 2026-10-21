cmake_minimum_required(VERSION 3.10)
project(MCP VERSION 2025.03.26 LANGUAGES CXX)

set(CMAKE_WARN_UNUSED_CLI YES)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT XCODE AND NOT MSVC AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

option(BUILD_SHARED_LIBS "build shared libraries" ${BUILD_SHARED_LIBS_DEFAULT})

if (WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:/utf-8>")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:/bigobj>")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/bigobj>")
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

option(MCP_SSL "Enable SSL support" OFF)
set(MCP_MAX_SESSIONS 10 CACHE STRING "Maximum concurrent MCP sessions (0 = unlimited)")
set(MCP_SESSION_TIMEOUT 30 CACHE STRING "Inactive session timeout in seconds (0 = disabled)")

if(MCP_SSL)
    find_package(OpenSSL 3.0.0 COMPONENTS Crypto SSL REQUIRED)
    include_directories(${OPENSSL_INCLUDE_DIR}) # before including cpp-httplib to avoid OpenSSL with lower version included 
    message(STATUS "OpenSSL include directory: ${OPENSSL_INCLUDE_DIR}")
    add_compile_definitions(MCP_SSL CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

if(DEFINED CPP_HTTPLIB_PATH)
    if (IS_DIRECTORY ${CPP_HTTPLIB_PATH} AND EXISTS ${CPP_HTTPLIB_PATH}/httplib.h)
        message(STATUS "Using user provided cpp-httplib: ${CPP_HTTPLIB_PATH}")
        include_directories(${CPP_HTTPLIB_PATH})
    else()
        message(FATAL_ERROR "user provided cpp-httplib not found")
    endif()
elseif(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/cpp-httplib)
    message(STATUS "Using git submodule provided cpp-httplib")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib/cpp-httplib)
else()
    message(STATUS "Using builtin provided cpp-httplib")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common/cpp-httplib)
endif()

if(DEFINED NLOHMANN_JSON_PATH)
    if (IS_DIRECTORY ${NLOHMANN_JSON_PATH}/include) #AND EXISTS ${NLOHMANN_JSON_PATH}/include/nlohmann/json.hpp)
        message(STATUS "Using user provided nlohmann-json: ${NLOHMANN_JSON_PATH}")
        include_directories(${NLOHMANN_JSON_PATH}/include)
    else()
        message(FATAL_ERROR "user provided nlohmann-json not found")
    endif()
elseif(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/nlohmann-json)
    message(STATUS "Using git submodule provided nlohmann-json")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib/nlohmann-json/include)
else()
    message(STATUS "Using builtin provided nlohmann-json")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common/nlohmann-json)
endif()

# Add MCP library
add_subdirectory(src)

# Add examples
add_subdirectory(examples)

# Add test directory
option(MCP_BUILD_TESTS "Build the tests" OFF)
if(MCP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
