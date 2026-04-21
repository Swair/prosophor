cmake_minimum_required(VERSION 3.20)

# Set default version if PACKAGE_VERSION not provided
if(NOT DEFINED PACKAGE_VERSION)
    set(PACKAGE_VERSION "1.0.0")
endif()

# Remove 'v' prefix if present (e.g., v0.2.0 -> 0.2.0)
string(REGEX REPLACE "^v" "" PACKAGE_VERSION_CLEAN ${PACKAGE_VERSION})

project(aicode VERSION ${PACKAGE_VERSION_CLEAN} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Set install prefix to "install" directory in build folder
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

add_subdirectory(main_src)
# add_subdirectory(tests)

# Install configuration files, roles, skills, and workspace
# Install to ~/.aicode (home directory) on all platforms
if(WIN32)
    # On Windows, use USERPROFILE environment variable
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}/.aicode" AICODE_INSTALL_DIR)
else()
    # On Unix-like systems, use HOME environment variable
    file(TO_CMAKE_PATH "$ENV{HOME}/.aicode" AICODE_INSTALL_DIR)
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/config/.aicode/ DESTINATION ${AICODE_INSTALL_DIR}
    PATTERN "settings.json" EXCLUDE  # Don't overwrite user's settings
)

# # Install assets directory to bin/assets (relative to executable)
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION bin/assets)