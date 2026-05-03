# Copyright 2026 Prosophor Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(prosophor_tests VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch GoogleTest
include(FetchContent)

############################################# googletest #####################################
# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/googletest-src")
    message(STATUS "Found googletest in thirdparty_src, using existing download")
    FetchContent_Declare(googletest SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/googletest-src")
else()
    message(STATUS "googletest not found, fetching from GitHub...")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/googletest-src
    )
endif()

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Common test configuration
set(TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../main_src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Common link libraries for tests
set(TEST_LINK_LIBS
    prosophor_core
    GTest::gtest
    GTest::gtest_main
)

# ========================================
# Individual Test Executables
# Each .cc file is compiled as a separate test program
# Automatically discovered from the tests directory
# ========================================

# Get all .cc files (excluding main.cc which is a helper)
file(GLOB TEST_SOURCES "unittest/*.cc")
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/unittest/main.cc")

# Create an executable for each test file
foreach(TEST_FILE ${TEST_SOURCES})
    # Get filename without path and extension
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    # Use the filename as the executable name (already has _test suffix)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_include_directories(${TEST_NAME} PRIVATE ${TEST_INCLUDE_DIRS})

    # Standalone tools (have own main, no gtest)
    if(TEST_NAME STREQUAL "agent_core_sync_test")
        target_link_libraries(${TEST_NAME} PRIVATE prosophor_core spdlog::spdlog nlohmann_json::nlohmann_json)
    else()
        # Regular gtest-based test
        target_link_libraries(${TEST_NAME} PRIVATE ${TEST_LINK_LIBS})

        # Register for CTest discovery
        gtest_discover_tests(${TEST_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TEST_PREFIX "${TEST_NAME}."
            DISCOVERY_TIMEOUT 60)
    endif()

    # Add to install list
    install(TARGETS ${TEST_NAME} DESTINATION bin/unitests)
endforeach()

# ========================================
# Benchmark Executables (standalone, no gtest)
# ========================================
file(GLOB BENCH_SOURCES "benchmark/*_test.cc")
foreach(BENCH_FILE ${BENCH_SOURCES})
    get_filename_component(BENCH_NAME ${BENCH_FILE} NAME_WE)
    add_executable(${BENCH_NAME} ${BENCH_FILE})
    target_include_directories(${BENCH_NAME} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(${BENCH_NAME} PRIVATE prosophor_core spdlog::spdlog nlohmann_json::nlohmann_json)
    install(TARGETS ${BENCH_NAME} DESTINATION bin/benchmark)
endforeach()
