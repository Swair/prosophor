cmake_minimum_required(VERSION 3.20)
project(prosophor_core VERSION 1.0.0 LANGUAGES CXX)


include(FetchContent)

############################################# spdlog #####################################
# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/spdlog-src")
    message(STATUS "Found spdlog in thirdparty_src, using existing download")
    FetchContent_Declare(spdlog SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/spdlog-src")
else()
    message(STATUS "spdlog not found, fetching from GitHub...")
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.0
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/spdlog-src
    )
endif()

FetchContent_MakeAvailable(spdlog)

############################################# nlohmann_json #####################################
# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/nlohmann_json-src")
    message(STATUS "Found nlohmann_json in thirdparty_src, using existing download")
    FetchContent_Declare(nlohmann_json SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/nlohmann_json-src")
else()
    message(STATUS "nlohmann_json not found, fetching from GitHub...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/nlohmann_json-src
    )
endif()

FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(media_engine)

# Required dependencies
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Collect source files excluding platform-specific ones
file(GLOB_RECURSE CC_FILES_LIST
    ${CMAKE_CURRENT_LIST_DIR}/agents/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/cli/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/common/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/components/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/core/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/managers/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/mcp/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/providers/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/services/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/tools/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/scene/home_screen.cc            # Home screen mode selector
    ${CMAKE_CURRENT_LIST_DIR}/scene/agent_state_observer.cc  # Always include for visualizer
    ${CMAKE_CURRENT_LIST_DIR}/scene/anime_character.cc       # Anime teacher character renderer
    ${CMAKE_CURRENT_LIST_DIR}/scene/galgame_mode.cc          # GALGAME mode scene
    ${CMAKE_CURRENT_LIST_DIR}/scene/sdl_app.cc               # SDL app entry
    ${CMAKE_CURRENT_LIST_DIR}/scene/ui_renderer.cc           # UI renderer
)

# PROSOPHOR_SDL_UI flag
if(PROSOPHOR_SDL_UI)
    add_compile_definitions(PROSOPHOR_SDL_UI)
endif()

# Exclude platform-specific implementation files
list(FILTER CC_FILES_LIST EXCLUDE REGEX ".*/cli/input_handler\\.cc$")
list(FILTER CC_FILES_LIST EXCLUDE REGEX ".*/cli/input_handler_posix\\.cc$")
list(FILTER CC_FILES_LIST EXCLUDE REGEX ".*/cli/input_handler_win32\\.cc$")

# Add new input abstraction layer files
list(APPEND CC_FILES_LIST
    ${CMAKE_CURRENT_LIST_DIR}/cli/terminal_input.cc
    ${CMAKE_CURRENT_LIST_DIR}/cli/output_manager.cc
)


# Add platform-specific input_handler based on the target platform
if(WIN32)
    list(APPEND CC_FILES_LIST
        ${CMAKE_CURRENT_LIST_DIR}/cli/input_handler_win32.cc
    )
else()
    list(APPEND CC_FILES_LIST
        ${CMAKE_CURRENT_LIST_DIR}/cli/input_handler_posix.cc
    )
endif()

add_library(prosophor_core STATIC ${CC_FILES_LIST})

target_include_directories(prosophor_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_definitions(prosophor_core PUBLIC
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    PROSOPHOR_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(prosophor_core
    PUBLIC
        media_engine
        spdlog::spdlog
        nlohmann_json::nlohmann_json
    PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(prosophor_core PUBLIC ws2_32 bcrypt)
endif()

if(PROSOPHOR_SDL_UI)
    add_executable(prosophor WIN32 main.cc)
else()
    add_executable(prosophor main.cc)
endif()

target_link_libraries(prosophor PRIVATE prosophor_core)
target_include_directories(prosophor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(prosophor_core PRIVATE -Wall -Wextra -O2)
    target_compile_options(prosophor PRIVATE -Wall -Wextra -O2)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(prosophor_core PRIVATE /W4 /Od /RTC1 /EHsc)
        target_compile_options(prosophor PRIVATE /W4 /Od /RTC1 /EHsc)
    else()
        target_compile_options(prosophor_core PRIVATE /W4 /O2 /EHsc)
        target_compile_options(prosophor PRIVATE /W4 /O2 /EHsc)
    endif()
endif()

install(TARGETS prosophor DESTINATION bin)
