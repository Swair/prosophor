# Copyright 2026 AiCode Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(aicode_tests VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Test executable
add_executable(aicode_test)

# Add test source files
file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)
target_sources(aicode_test PRIVATE ${TEST_SOURCES})

# Include directories
target_include_directories(aicode_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../main_src
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries - aicode_core already includes spdlog and nlohmann_json
target_link_libraries(aicode_test
    PRIVATE
        aicode_core
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aicode_test PRIVATE -Wall -Wextra -O2)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(aicode_test PRIVATE /W4 /Od /RTC1 /EHsc)
    else()
        target_compile_options(aicode_test PRIVATE /W4 /O2 /EHsc)
    endif()
endif()

# Register tests
gtest_discover_tests(aicode_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    TEST_PREFIX "aicode."
    DISCOVERY_TIMEOUT 60
)
