cmake_minimum_required(VERSION 3.20)

# Set default version if PACKAGE_VERSION not provided
if(NOT DEFINED PACKAGE_VERSION)
    set(PACKAGE_VERSION "1.0.0")
endif()

# Remove 'v' prefix if present (e.g., v0.2.0 -> 0.2.0)
string(REGEX REPLACE "^v" "" PACKAGE_VERSION_CLEAN ${PACKAGE_VERSION})

project(prosophor VERSION ${PACKAGE_VERSION_CLEAN} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Suppress CMP0148 warning from third-party deps (FindPythonInterp removed)
if(POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD)
endif()

# Set install prefix to "install" directory in build folder
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

include(FetchContent)

############################################# llama.cpp (optional) #####################################
option(PROSOPHOR_BUILD_LLAMA "Build llama.cpp for local model inference (llama-server)" ON)

if(PROSOPHOR_BUILD_LLAMA)
    if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/llamacpp-src")
        message(STATUS "Found llama.cpp in thirdparty_src, using existing download")
        FetchContent_Declare(llamacpp SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/llamacpp-src")
    else()
        message(STATUS "llama.cpp not found, fetching from GitHub...")
        FetchContent_Declare(
            llamacpp
            GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
            GIT_TAG master
            GIT_SHALLOW TRUE
            GIT_SUBMODULES ""
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/llamacpp-src
        )
    endif()

    set(LLAMA_BUILD_COMMON  ON  CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS   ON  CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER  ON  CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(llamacpp)
endif()

add_subdirectory(main_src)

# if(NOT WIN32)
#     add_subdirectory(tests)
# endif()

# Install configuration files, roles, skills, and workspace
# Install to ~/.prosophor (home directory) on all platforms
if(WIN32)
    # On Windows, use USERPROFILE environment variable
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}/.prosophor" PROSOPHOR_INSTALL_DIR)
else()
    # On Unix-like systems, use HOME environment variable
    file(TO_CMAKE_PATH "$ENV{HOME}/.prosophor" PROSOPHOR_INSTALL_DIR)
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/config/.prosophor/ DESTINATION ${PROSOPHOR_INSTALL_DIR})

# # Install assets directory to bin/assets (relative to executable)
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION bin/assets)