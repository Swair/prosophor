cmake_minimum_required(VERSION 3.10)

project(media_engine)

include(FetchContent)


############################################# SDL3 #####################################
set(SDL_SHARED OFF CACHE BOOL "Build shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build static library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build tests" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
set(SDL_JACK OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
set(SDL_LIBUSB OFF CACHE BOOL "Disable libusb support" FORCE)

# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3-src")
    message(STATUS "Found SDL3 in thirdparty_src, using existing download")
    FetchContent_Declare(SDL3 SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3-src")
else()
    message(STATUS "SDL3 not found, fetching from GitHub...")
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG main
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3-src
    )
endif()

FetchContent_MakeAvailable(SDL3)

############################################# SDL3_ttf #####################################
set(SDLTTF_VENDORED ON)
set(SDLTTF_TESTS OFF CACHE BOOL "Build tests" FORCE)

# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_ttf-src")
    message(STATUS "Found SDL3_ttf in thirdparty_src, using existing download")
    FetchContent_Declare(SDL3_ttf SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_ttf-src")
else()
    message(STATUS "SDL3_ttf not found, fetching from GitHub...")
    FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.0
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_ttf-src
    )
endif()

FetchContent_MakeAvailable(SDL3_ttf)

############################################# SDL3_mixer #####################################
set(SDLMIXER_MIDI_NATIVE OFF)
set(SDLMIXER_GME OFF)
set(SDLMIXER_WAVPACK OFF)
set(SDLMIXER_MOD OFF)
set(SDLMIXER_MOD_XMP OFF)
set(SDLMIXER_OPUS OFF)
set(SDLMIXER_MP3_MPG123 OFF)
set(SDLMIXER_VORBIS_VORBISFILE OFF)
set(SDLMIXER_FLAC_LIBFLAC OFF)
set(SDLMIXER_VENDORED ON)
set(SDLMIXER_TESTS OFF CACHE BOOL "Build tests" FORCE)

# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_mixer-src")
    message(STATUS "Found SDL3_mixer in thirdparty_src, using existing download")
    FetchContent_Declare(SDL3_mixer SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_mixer-src")
else()
    message(STATUS "SDL3_mixer not found, fetching from GitHub...")
    FetchContent_Declare(
        SDL3_mixer
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
        GIT_TAG release-3.2.0
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_mixer-src
    )
endif()

FetchContent_MakeAvailable(SDL3_mixer)

############################################# SDL3_image #####################################
set(SDL3_IMAGE_VENDORED ON)
set(SDL3_IMAGE_BMP OFF)
set(SDL3_IMAGE_JPEG OFF)
set(SDL3_IMAGE_WEBP OFF)
set(SDL3_IMAGE_PNG_LIBPNG OFF)
set(SDL3_IMAGE_TESTS OFF CACHE BOOL "Build tests" FORCE)
set(SDL3_IMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDL3_IMAGE_AVIF OFF CACHE BOOL "" FORCE)

# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_image-src")
    message(STATUS "Found SDL3_image in thirdparty_src, using existing download")
    FetchContent_Declare(SDL3_image SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_image-src")
else()
    message(STATUS "SDL3_image not found, fetching from GitHub...")
    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG release-3.2.0
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/sdl3_image-src
    )
endif()

FetchContent_MakeAvailable(SDL3_image)

############################################# ImGui #####################################
# 检查：thirdparty_src 目录存在则使用，否则下载
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty_src/imgui-src")
    message(STATUS "Found ImGui in thirdparty_src, using existing download")
    FetchContent_Declare(imgui SOURCE_DIR "${CMAKE_SOURCE_DIR}/thirdparty_src/imgui-src")
else()
    message(STATUS "ImGui not found, fetching from GitHub...")
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG docking
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty_src/imgui-src
    )
endif()

FetchContent_MakeAvailable(imgui)

# 配置 ImGui 源文件
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

# 创建 ImGui 库
add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3 spdlog::spdlog)

# ImGui 组件封装库
set(IMGUI_WIDGET_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/sdl_interface/imgui_widget.cc
)
add_library(imgui_widget_lib STATIC ${IMGUI_WIDGET_SOURCES})
target_include_directories(imgui_widget_lib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(imgui_widget_lib PUBLIC imgui_lib)

############################################# media_engine #####################################

file(GLOB_RECURSE CPP_FILES_LIST ${CMAKE_CURRENT_LIST_DIR}/*.cc)

# 添加可执行文件
add_library(${PROJECT_NAME} STATIC
    ${CPP_FILES_LIST}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(${PROJECT_NAME}
PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/media
PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/imgui_interface
    ${CMAKE_CURRENT_LIST_DIR}/sdl_interface
    ${CMAKE_CURRENT_LIST_DIR}/../common
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    spdlog::spdlog
    imgui_lib
    imgui_widget_lib
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    SDL3::SDL3
)
