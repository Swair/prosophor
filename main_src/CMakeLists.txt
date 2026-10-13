cmake_minimum_required(VERSION 3.20)
project(prosophor_core VERSION ${PACKAGE_VERSION_CLEAN} LANGUAGES CXX C)

# ---- 通用依赖查找（各形态/各 target 共享；CURL/OpenSSL/OpenMP 已在根 CMakeLists）----
find_package(Threads REQUIRED)

# ============================================================================
# 应用形态选择（每个应用一个正向开关；一次构建仅一个形态）
#   PROSOPHOR_NUTRIBOT / PROSOPHOR_SDL_UI / PROSOPHOR_AICODING
#   三者全关 -> 默认 ai_coding（与 prosophor 历史默认一致）
# ============================================================================
set(_FORM_COUNT 0)
foreach(_form PROSOPHOR_NUTRIBOT PROSOPHOR_SDL_UI PROSOPHOR_AICODING)
    if(${_form})
        math(EXPR _FORM_COUNT "${_FORM_COUNT} + 1")
    endif()
endforeach()
if(_FORM_COUNT GREATER 1)
    message(FATAL_ERROR
        "应用形态互斥：PROSOPHOR_NUTRIBOT / PROSOPHOR_SDL_UI / PROSOPHOR_AICODING "
        "一次只能开启一个")
endif()
if(_FORM_COUNT EQUAL 0)
    set(PROSOPHOR_AICODING ON CACHE BOOL "Build ai_coding terminal application (default form)" FORCE)
    message(STATUS "未指定应用形态 -> 默认 ai_coding")
endif()
unset(_FORM_COUNT)

# 形态宏提前定义：prosophor_core（provider_router 等）编译期即可见
if(PROSOPHOR_NUTRIBOT)
    add_compile_definitions(PROSOPHOR_NUTRIBOT)
endif()

# ============================================================================
# prosophor_core: common + platform + core logic (single static library)
# Used by both TUI and SDL builds
# ============================================================================
file(GLOB_RECURSE CORE_SOURCES_ALL
    ${CMAKE_CURRENT_LIST_DIR}/base/common/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/base/rtc_wrapper/resampler.cc
    ${CMAKE_CURRENT_LIST_DIR}/base/rtc_wrapper/vad_processor.cc
    ${CMAKE_CURRENT_LIST_DIR}/base/rtc_wrapper/rtc/*.c
    ${CMAKE_CURRENT_LIST_DIR}/base/rtc_wrapper/rtc/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/prosophor_core/*.cc
)

# Platform-specific sources
if(WIN32)
    set(PLATFORM_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/base/platform/platform.cc
        ${CMAKE_CURRENT_LIST_DIR}/base/platform/input_handler_win32.cc
        ${CMAKE_CURRENT_LIST_DIR}/base/platform/pipe_handler_win32.cc
    )
else()
    set(PLATFORM_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/base/platform/platform.cc
        ${CMAKE_CURRENT_LIST_DIR}/base/platform/input_handler_posix.cc
        ${CMAKE_CURRENT_LIST_DIR}/base/platform/pipe_handler_posix.cc
    )
endif()
list(APPEND CORE_SOURCES_ALL ${PLATFORM_SOURCES})

if(NOT PROSOPHOR_BUILD_LLAMA)
    list(FILTER CORE_SOURCES_ALL EXCLUDE REGEX "llamacpp_provider\\.cc$")
endif()

# rtc/media 栈只为桌面 SDL(音频会话)服务，且其 typedefs.h 不支持 arm64；
# websocket_client 依赖 libcurl >= 7.86 实验性 websocket API —— 本地终端形态裁剪
if(PROSOPHOR_NUTRIBOT OR PROSOPHOR_AICODING)
    list(FILTER CORE_SOURCES_ALL EXCLUDE REGEX "rtc_wrapper")
    list(FILTER CORE_SOURCES_ALL EXCLUDE REGEX "websocket_client\\.cc$")
endif()

# edge-tts provider 依赖 libcurl >= 7.86 实验性 websocket API（curl_ws_*）。
# 按平台能力探测：libcurl 达标（Windows/新版系统）则保留，低版本剔除并禁注册
if(DEFINED CURL_VERSION_STRING AND NOT CURL_VERSION_STRING VERSION_LESS "7.86")
    message(STATUS "libcurl ${CURL_VERSION_STRING}: edge-tts provider 可用")
else()
    message(STATUS "libcurl ${CURL_VERSION_STRING}: 无 websocket 实验 API -> edge-tts 剔除")
    list(FILTER CORE_SOURCES_ALL EXCLUDE REGEX "edge_tts_provider\\.cc$")
    add_compile_definitions(PROSOPHOR_NO_EDGE_TTS)
endif()


# aidgen LLM provider 源在 core GLOB 内；仅 nutribot 形态使用。
# aidgen_provider.cc 是仓库内唯一包含 aidgen.hpp / aidlite.hpp / opencv 的文件
# （SDK 接入点），必须过滤——否则非 nutribot 形态会在没有这些头文件的环境里编译它。
# aidvoice（本地 ASR/TTS）同理：aidvoice_asr_provider.cc / aidvoice_tts_provider.cc
# 是 aidvoice.hpp 的唯一接入点；两处文件命名保持 aidvoice_ 前缀以利单条正则。
# 过滤须在 add_library(prosophor_core) 前执行（链接段见下方 nutribot 形态段）
if(NOT PROSOPHOR_NUTRIBOT)
    list(FILTER CORE_SOURCES_ALL EXCLUDE REGEX "aidgen_provider\\.cc$")
    list(FILTER CORE_SOURCES_ALL EXCLUDE REGEX "aidvoice_(asr|tts)_provider\\.cc$")
endif()

add_library(prosophor_core STATIC ${CORE_SOURCES_ALL})

target_include_directories(prosophor_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/prosophor_core>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/base>
)

target_compile_definitions(prosophor_core PUBLIC
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    PROSOPHOR_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(prosophor_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
    PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(prosophor_core PUBLIC ws2_32 bcrypt iphlpapi)
endif()

if(PROSOPHOR_BUILD_LLAMA)
    target_include_directories(prosophor_core
        PUBLIC
            SYSTEM ${CMAKE_SOURCE_DIR}/thirdparty_src/llamacpp-src
            SYSTEM ${CMAKE_SOURCE_DIR}/thirdparty_src/llamacpp-src/common
            SYSTEM ${CMAKE_SOURCE_DIR}/thirdparty_src/llamacpp-src/ggml/include
    )
    target_compile_definitions(prosophor_core PUBLIC PROSOPHOR_HAS_LOCAL_MODEL)
    target_link_libraries(prosophor_core PUBLIC llama ggml llama-common)
endif()

# ============================================================================
# web_server 应用（正向开关 PROSOPHOR_WEB=ON；默认 OFF，需要时显式开启）
# HTTP + WebSocket 多用户服务端(prosophor_web)。含 IM 渠道层(原 im_gateway):
# channel.h(ImChannel 抽象)+ web_gateway(会话编排)+ web_channel + web_protocol。
# 依赖 prosophor_core;不链接 SDL/media_engine/components 等 UI 层。
# httplib 单头文件走 SYSTEM include（需先 vendor 到 thirdparty_src/cpp-httplib）。
# ============================================================================
if(PROSOPHOR_WEB)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/thirdparty_src/cpp-httplib/httplib.h)
        message(FATAL_ERROR
            "PROSOPHOR_WEB=ON 需要 cpp-httplib 头文件，请先 vendor：\n"
            "  git clone --depth 1 https://github.com/yhirose/cpp-httplib "
            "${CMAKE_SOURCE_DIR}/thirdparty_src/cpp-httplib")
    endif()

    file(GLOB_RECURSE WEB_SERVER_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/web_server/*.cc
    )
    list(FILTER WEB_SERVER_SOURCES EXCLUDE REGEX "main_web\\.cc$")

    add_library(prosophor_web_server STATIC ${WEB_SERVER_SOURCES})

    target_include_directories(prosophor_web_server
        SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/thirdparty_src/cpp-httplib
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/web_server>
    )

    target_link_libraries(prosophor_web_server
        PUBLIC prosophor_core
    )

    if(WIN32)
        target_link_libraries(prosophor_web_server PUBLIC ws2_32)
    endif()

    add_executable(prosophor_web
        ${CMAKE_CURRENT_LIST_DIR}/web_server/main_web.cc
        ${CMAKE_CURRENT_LIST_DIR}/resources/app.rc
    )

    # include 路径经 prosophor_web_server 的 PUBLIC BUILD_INTERFACE 传递
    target_link_libraries(prosophor_web PRIVATE prosophor_web_server)

    # llama-common 自带 vendor cpp-httplib(v0.45,编译为静态库),与本项目 v0.53
    # header-only 符号冲突;取第一个定义(本项目 v0.53,API 向后兼容)。
    target_link_options(prosophor_web PRIVATE "-Wl,--allow-multiple-definition")

    # 前端产物(可选:PROSOPHOR_BUILD_WEB_FRONTEND=ON 时 npm 构建,默认 OFF)
    option(PROSOPHOR_BUILD_WEB_FRONTEND "Build web frontend via npm" OFF)
    if(PROSOPHOR_BUILD_WEB_FRONTEND)
        find_program(NPM_EXECUTABLE npm)
        if(NPM_EXECUTABLE)
            add_custom_target(prosophor_web_frontend
                COMMAND ${NPM_EXECUTABLE} ci
                COMMAND ${NPM_EXECUTABLE} run build
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/web
                BYPRODUCTS ${CMAKE_SOURCE_DIR}/web/dist
                COMMENT "Building web frontend (npm ci && npm run build)"
            )
            add_dependencies(prosophor_web prosophor_web_frontend)
        else()
            message(WARNING "PROSOPHOR_BUILD_WEB_FRONTEND=ON but npm not found; frontend skipped")
        endif()
    endif()

    # 安装（随 web 段）
    install(TARGETS prosophor_web DESTINATION bin)
    # 尾部斜杠:把 dist 内容直接装进 bin/web-dist(运行时静态根),而不是 web-dist/dist
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/web/dist/ DESTINATION bin/web-dist OPTIONAL)
endif()

# ============================================================================
# ai_coding 应用（正向开关 PROSOPHOR_AICODING；形态互斥见文件顶部）
# ============================================================================
if(PROSOPHOR_AICODING)
    add_compile_definitions(PROSOPHOR_AICODING)
    file(GLOB_RECURSE TUI_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/ai_coding/*.cc
    )

    # 终端运行：无 GUI 资源文件
    add_executable(prosophor ${TUI_SOURCES} main.cc)

    target_include_directories(prosophor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_coding
    )

    target_link_libraries(prosophor PRIVATE prosophor_core)
endif()

# ============================================================================
# SDL build: prosophor_core + media_engine + virtual_sprite
# ============================================================================
if(PROSOPHOR_SDL_UI)
    add_subdirectory(base/media_engine)

    target_compile_definitions(media_engine PRIVATE PROSOPHOR_VERSION="${PROJECT_VERSION}")

    add_compile_definitions(PROSOPHOR_SDL_UI)

    # components/ 已并入 virtual_sprite/（virtual_sprite/components/），
    # GLOB_RECURSE 自动覆盖，不再单独 glob。
    file(GLOB_RECURSE GUI_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/virtual_sprite/*.cc
    )
    add_executable(prosophor
        main.cc
        ${GUI_SOURCES}
        ${CMAKE_CURRENT_LIST_DIR}/resources/app.rc
    )

    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/resources/app.rc
        PROPERTIES SOURCE_FILE_DEPENDENCIES ${CMAKE_CURRENT_LIST_DIR}/resources/app.ico
    )

    # if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo|MinSizeRel")
    if(WIN32)
        set_target_properties(prosophor PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    target_include_directories(prosophor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/base
        ${CMAKE_CURRENT_SOURCE_DIR}/virtual_sprite
    )

    target_link_libraries(prosophor PRIVATE
        prosophor_core
        media_engine
    )
endif()


# ============================================================================
# nutribot 形态（PROSOPHOR_NUTRIBOT=ON）：QNN 本地 NPU 营养管家
#   无模块级 CMakeLists（同 ai_coding/web_server：源由本文件 GLOB 收编）；
#   单测源码位于仓库顶级 tests/nutribot/（零依赖微框架）。
#   统一入口 main_src/main.cc 按宏 PROSOPHOR_NUTRIBOT 分支（已预埋）。
# ============================================================================
if(PROSOPHOR_NUTRIBOT)
    # opencv 头路径（本段内多处共用；静态库 PRIVATE 的 include 不传导给 exe，
    # 故 prosophor_core 与 prosophor 各自要写一次 —— 见下方 opencv 链接段）
    set(NR_OPENCV_INCLUDE /usr/include/opencv4)

    file(GLOB_RECURSE NUTRIBOT_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/nutribot/*.cc
    )
    add_executable(prosophor
        main.cc
        ${NUTRIBOT_SOURCES}
    )
    target_include_directories(prosophor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/nutribot
        ${CMAKE_CURRENT_SOURCE_DIR}/prosophor_core
        ${CMAKE_CURRENT_SOURCE_DIR}/base
        # scene_monitor.cc（帧差运动）编译期需要 opencv 头（见下方 opencv 段）
        ${NR_OPENCV_INCLUDE}
    )
    # aidgen LLM provider：随 prosophor_core 链接（nutribot 形态硬依赖，NPU 本地推理；
    # 缺 SDK 直接配置失败；源过滤见 core 收集段）
    find_library(NR_AIDGEN_LIB NAMES aidgen PATHS /usr/local/lib REQUIRED)
    target_include_directories(prosophor_core PRIVATE /usr/local/include)
    target_link_libraries(prosophor_core PRIVATE ${NR_AIDGEN_LIB})
    target_link_options(prosophor_core PRIVATE "-Wl,-rpath,/usr/local/lib")
    message(STATUS "aidgen-sdk：已并入 prosophor_core（nutribot 硬依赖，NPU 本地推理）")

    # aidvoice 语音（本地 ASR/TTS）：SenseVoiceSmall + MeloTTS，NPU 离线推理；
    # 头/库路径与 rpath 同上（/usr/local/include + /usr/local/lib 已就位）
    find_library(NR_AIDVOICE_LIB NAMES aidvoice PATHS /usr/local/lib REQUIRED)
    target_link_libraries(prosophor_core PRIVATE ${NR_AIDVOICE_LIB})
    message(STATUS "aidvoice-sdk：已并入 prosophor_core（ASR/TTS 本地 NPU）")

    # aidgen-vl 多模态：aidlite(视觉编码器) + opencv(图像解码/缩放) ——
    # 官方 aidmlm 例程同配方；视觉链路内联在 aidgen_provider.cc
    # （open include 变量 NR_OPENCV_INCLUDE 在本段开头定义）
    find_library(NR_AIDLITE_LIB NAMES aidlite PATHS /usr/local/lib REQUIRED)
    find_library(NR_OPENCV_CORE NAMES opencv_core REQUIRED)
    find_library(NR_OPENCV_IMGPROC NAMES opencv_imgproc REQUIRED)
    find_library(NR_OPENCV_IMGCODECS NAMES opencv_imgcodecs REQUIRED)
    target_include_directories(prosophor_core PRIVATE ${NR_OPENCV_INCLUDE})
    target_link_libraries(prosophor_core PRIVATE
        ${NR_AIDLITE_LIB} ${NR_OPENCV_CORE} ${NR_OPENCV_IMGPROC} ${NR_OPENCV_IMGCODECS})
    message(STATUS "aidlite + opencv：已并入 prosophor_core（aidgen-vl 视觉链路）")

    # 音频 I/O：ALSA 直连（nutribot/audio_io.cc；/usr/include 默认即系统包含路径）
    target_link_libraries(prosophor PRIVATE prosophor_core asound)
endif()

# ---- 主形态 prosophor 安装（置于文件尾：nutribot 形态的 prosophor 在此才定义；
#       prosophor_web 的安装在其 web 段内，见上）----
install(TARGETS prosophor DESTINATION bin)

# ============================================================================
# Compiler flags（置于文件尾：此时全部 target 已定义完毕 ——
#   nutribot 段新增的 nr_core/prosophor 等一并覆盖，无需逐段打补丁；
#   新增 target 加入列表即可；TARGET 守卫容忍条件形态未开启的情况）
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(PROSOPHOR_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Wshadow -Werror -O2)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(PROSOPHOR_COMPILE_OPTIONS /W4 /WX /Od /RTC1 /EHsc)
    else()
        set(PROSOPHOR_COMPILE_OPTIONS /W4 /WX /O2 /EHsc)
    endif()
endif()
foreach(_tgt prosophor_core prosophor prosophor_web_server prosophor_web
             nr_core)
    if(PROSOPHOR_COMPILE_OPTIONS AND TARGET ${_tgt})
        target_compile_options(${_tgt} PRIVATE ${PROSOPHOR_COMPILE_OPTIONS})
    endif()
endforeach()
unset(_tgt)
