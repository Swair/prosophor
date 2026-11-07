cmake_minimum_required(VERSION 3.20)

# Set Windows version to Windows 10 (required by cpp-httplib)
# and math constants for MinGW
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 _USE_MATH_DEFINES M_PI=3.14159265358979323846)
endif()

if(NOT DEFINED PACKAGE_VERSION)
    # 深度整合：nutrition_robot/aidgen 形态以普通变体构建即可，
    # 不强制每个构建入口都传版本号（仍可用 -DPACKAGE_VERSION 覆盖）
    set(PACKAGE_VERSION 0.8.1)
    message(STATUS "PACKAGE_VERSION 未指定，使用默认 ${PACKAGE_VERSION}")
endif()

# Remove 'v' prefix if present (e.g., v0.2.0 -> 0.2.0)
string(REGEX REPLACE "^v" "" PACKAGE_VERSION_CLEAN ${PACKAGE_VERSION})

# C 用于 SDL3 等 FetchContent 子工程（子目录里的 project(LANGUAGES C) 不会在首次
# configure 自行探测编译器），以及 voice_interaction 的 C 源码
project(prosophor VERSION ${PACKAGE_VERSION_CLEAN} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Static analysis: clang-tidy ───────────────────────────────────────────
find_program(CLANG_TIDY_BIN clang-tidy)
if(CLANG_TIDY_BIN)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_BIN};--checks=bugprone-use-after-move")
    message(STATUS "clang-tidy found: ${CLANG_TIDY_BIN} (checks: bugprone-use-after-move)")
else()
    message(STATUS "clang-tidy not found — static analysis disabled")
endif()

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
# GPU 后端候选：默认 CUDA 和 Vulkan 都参与探测，实际用哪个由下方能力探测决定，
# 优先级固定 CUDA → Vulkan → CPU。探测看的是"工具链真的可用"而不是平台名：
# Windows + MSVC 可用 CUDA；MinGW 下 nvcc 找不到 cl.exe，探测失败自动落回 Vulkan。
set(_PROSOPHOR_DEFAULT_LLAMA_CUDA ON)
set(_PROSOPHOR_DEFAULT_LLAMA_VULKAN ON)
# 平台形态默认（QNN 板级分支，如 Rhino Pi-X1 / QCS8550）：
#   LLM 走本地 NPU（aidgen-sdk）-> llamacpp 默认 OFF，需要桌面 llama 时显式 ON
#   UI 走终端形态 -> SDL 桌面精灵默认 OFF，需要时显式 ON
option(PROSOPHOR_BUILD_LLAMA "Build llama.cpp for local model inference (in-process)" OFF)
option(PROSOPHOR_BUILD_LLAMA_CUDA "Build llama.cpp with CUDA GPU support" ${_PROSOPHOR_DEFAULT_LLAMA_CUDA})
option(PROSOPHOR_BUILD_LLAMA_VULKAN "Build llama.cpp with Vulkan GPU support" ${_PROSOPHOR_DEFAULT_LLAMA_VULKAN})
option(PROSOPHOR_SDL_UI "Build SDL desktop sprite UI application" OFF)
option(PROSOPHOR_NUTRIBOT "Build NutriBot (QNN local-NPU nutrition agent) application" OFF)
option(PROSOPHOR_AICODING "Build ai_coding terminal application (default form)" OFF)
# web 为独立服务端形态：可与任一主 UI 形态共存，需要时显式开启
option(PROSOPHOR_WEB "Build web server application (prosophor_web)" OFF)
# Windows SDL 本地 TTS/ASR：MeloTTS + SenseVoice（sherpa-onnx 预编译运行时）
option(PROSOPHOR_BUILD_SHERPA_VOICE "Build sherpa-onnx local TTS/ASR backends (Windows)" OFF)

############################################# Debug borders (optional) #####################################

# llama.cpp version: dbe9c0c8c (2026-05-26, upstream master)
# Local patch: support min32gw (8bf9670aa, on feat/support_mingw32 branch)
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
    set(LLAMA_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER  OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

    # GPU 后端选择：CUDA → Vulkan → CPU。先依次探测并选定后端，再用
    # if/elseif/else 一次性写 ggml 开关 —— 未选中的后端每次 configure 都显式关闭，
    # 避免同一构建目录切换后端时残留旧的 GGML_* 缓存。
    set(_prosophor_llama_backend cpu)

    if(PROSOPHOR_BUILD_LLAMA_CUDA)
        # 能力探测而非平台硬编码：找到 CUDAToolkit 后交给 check_language 用当前
        # host 编译器实际试跑 nvcc。nvcc 不支持的 host 编译器（如 MinGW g++）会
        # 得到 NOTFOUND，自动落到 Vulkan / CPU，而不是在构建阶段才失败。
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND)
            # Linux 下 nvcc 常在 /usr/local/cuda/bin，未必在 PATH，显式指给探测
            set(ENV{CUDACXX} "${CUDAToolkit_NVCC_EXECUTABLE}")
            include(CheckLanguage)
            check_language(CUDA)
        endif()

        if(CMAKE_CUDA_COMPILER)
            set(_prosophor_llama_backend cuda)
        elseif(CUDAToolkit_FOUND)
            message(WARNING "llama.cpp: CUDAToolkit found but nvcc cannot use the current host compiler, skipped")
        else()
            message(STATUS "llama.cpp: CUDAToolkit not found, skipped")
        endif()
    endif()

    if(NOT _prosophor_llama_backend STREQUAL "cuda" AND PROSOPHOR_BUILD_LLAMA_VULKAN)
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            set(_prosophor_llama_backend vulkan)
        else()
            message(WARNING "llama.cpp: Vulkan not found")
        endif()
    endif()

    if(_prosophor_llama_backend STREQUAL "cuda")
        set(GGML_CUDA ON CACHE BOOL "" FORCE)
        set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
        if(NOT CMAKE_CUDA_ARCHITECTURES)
            set(CMAKE_CUDA_ARCHITECTURES "75;89" CACHE STRING "CUDA architectures (75=Turing, 89=Ada)")
        endif()
        message(STATUS "llama.cpp: CUDA GPU support enabled (archs=${CMAKE_CUDA_ARCHITECTURES})")
    elseif(_prosophor_llama_backend STREQUAL "vulkan")
        set(GGML_CUDA OFF CACHE BOOL "" FORCE)
        set(GGML_VULKAN ON CACHE BOOL "" FORCE)
        message(STATUS "llama.cpp: Vulkan GPU support enabled")
    else()
        set(GGML_CUDA OFF CACHE BOOL "" FORCE)
        set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
        message(STATUS "llama.cpp: CPU-only backend")
    endif()

    FetchContent_MakeAvailable(llamacpp)
endif()

############################################# sherpa-onnx (Windows local TTS/ASR) #####################################
# 预编译运行时（VS2022 shared-MT，含 TTS）：不参与编译，只提供 C API 头 / 导入库 / DLL。
# MT 变体静态链接 CRT，避免额外 vcruntime 依赖；MinGW 可直接链接其 .lib。
# 优先使用 thirdparty_src 内的离线解压目录，缺失时从 GitHub Releases 下载。
if(PROSOPHOR_BUILD_SHERPA_VOICE)
    if(NOT WIN32)
        message(FATAL_ERROR "PROSOPHOR_BUILD_SHERPA_VOICE 仅支持 Windows 预编译运行时")
    endif()

    set(SHERPA_ONNX_VERSION 2.0.1)
    set(SHERPA_ONNX_PACKAGE sherpa-onnx-v${SHERPA_ONNX_VERSION}-win-x64-shared-MT-Release)
    set(SHERPA_ONNX_ROOT "${CMAKE_SOURCE_DIR}/thirdparty_src/sherpa-onnx-win-x64")

    if(NOT EXISTS "${SHERPA_ONNX_ROOT}/include/sherpa-onnx/c-api/c-api.h")
        set(SHERPA_ONNX_ROOT "${CMAKE_BINARY_DIR}/_deps/${SHERPA_ONNX_PACKAGE}")
        if(NOT EXISTS "${SHERPA_ONNX_ROOT}/include/sherpa-onnx/c-api/c-api.h")
            message(STATUS "sherpa-onnx not found, downloading v${SHERPA_ONNX_VERSION}...")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
            file(DOWNLOAD
                "https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_ONNX_VERSION}/${SHERPA_ONNX_PACKAGE}.tar.bz2"
                "${CMAKE_BINARY_DIR}/_deps/${SHERPA_ONNX_PACKAGE}.tar.bz2"
                EXPECTED_HASH SHA256=dc703575a68594435f53dec93df775ff4913af851a196c0537163f21d8d2364e
                TLS_VERIFY ON
                SHOW_PROGRESS)
            file(ARCHIVE_EXTRACT INPUT
                "${CMAKE_BINARY_DIR}/_deps/${SHERPA_ONNX_PACKAGE}.tar.bz2"
                DESTINATION "${CMAKE_BINARY_DIR}/_deps")
        endif()
    endif()

    set(SHERPA_ONNX_INCLUDE "${SHERPA_ONNX_ROOT}/include")
    set(SHERPA_ONNX_IMPORT_LIB "${SHERPA_ONNX_ROOT}/lib/sherpa-onnx-c-api.lib")
    message(STATUS "sherpa-onnx: 预编译运行时就绪 (${SHERPA_ONNX_ROOT})")

    # sherpa-onnx-c-api.dll + onnxruntime.dll + onnxruntime_providers_shared.dll
    install(DIRECTORY "${SHERPA_ONNX_ROOT}/lib/" DESTINATION bin
            FILES_MATCHING PATTERN "*.dll")
endif()

############################################# spdlog #####################################
# 优先使用系统 dev 包（含 CMake config，如 libspdlog-dev / spdlogConfig.cmake），
# 缺失时才走 thirdparty_src / GitHub FetchContent 兜底 —— 板级离线构建友好。
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
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
else()
    message(STATUS "使用系统 spdlog (${spdlog_VERSION})")
endif()

############################################# nlohmann_json #####################################
# 优先使用系统 dev 包（nlohmann-json3-dev / nlohmann_jsonConfig.cmake）
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
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
else()
    message(STATUS "使用系统 nlohmann_json (${nlohmann_json_VERSION})")
endif()

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenMP)

add_subdirectory(main_src)



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

# ============================================================================
# Runtime DLL bundling (MinGW on Windows)
# ============================================================================
if(WIN32 AND MINGW)
    # Use compiler's directory to find MinGW DLLs (more reliable than MINGW_PREFIX env var)
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    # Runtime DLLs (OpenMP from MinGW for llama.cpp parallel CPU inference)
    install(FILES
        "${MINGW_BIN_DIR}/libcurl-4.dll"
        "${MINGW_BIN_DIR}/libgomp-1.dll"
        "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
        "${MINGW_BIN_DIR}/libstdc++-6.dll"
        "${MINGW_BIN_DIR}/libwinpthread-1.dll"
        "${MINGW_BIN_DIR}/zlib1.dll"
        "${MINGW_BIN_DIR}/libcrypto-3-x64.dll"
        "${MINGW_BIN_DIR}/libssl-3-x64.dll"
        "${MINGW_BIN_DIR}/libbrotlicommon.dll"
        "${MINGW_BIN_DIR}/libbrotlidec.dll"
        "${MINGW_BIN_DIR}/libiconv-2.dll"
        "${MINGW_BIN_DIR}/libidn2-0.dll"
        "${MINGW_BIN_DIR}/libintl-8.dll"
        "${MINGW_BIN_DIR}/libnghttp2-14.dll"
        "${MINGW_BIN_DIR}/libnghttp3-9.dll"
        "${MINGW_BIN_DIR}/libngtcp2-16.dll"
        "${MINGW_BIN_DIR}/libngtcp2_crypto_ossl-0.dll"
        "${MINGW_BIN_DIR}/libpsl-5.dll"
        "${MINGW_BIN_DIR}/libssh2-1.dll"
        "${MINGW_BIN_DIR}/libunistring-5.dll"
        "${MINGW_BIN_DIR}/libzstd.dll"
        "${MINGW_BIN_DIR}/vulkan-1.dll"
        DESTINATION bin
    )

    # CA certificate bundle for SSL verification
    install(FILES
        "${MINGW_BIN_DIR}/../etc/ssl/certs/ca-bundle.crt"
        DESTINATION bin
    )
endif()

# ============================================================================
# CPack packaging
# ============================================================================
set(CPACK_PACKAGE_NAME "Prosophor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AI Desktop Companion")
set(CPACK_PACKAGE_VENDOR "Prosophor Contributors")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Prosophor")
set(CPACK_PACKAGE_EXECUTABLES "prosophor" "Prosophor")

if(WIN32)
    # Try NSIS first, fall back to ZIP (which requires no external tools)
    find_program(NSIS_MAKENSIS makensis)
    if(NSIS_MAKENSIS)
        set(CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_DISPLAY_NAME "Prosophor")
        set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
        set(CPACK_NSIS_CREATE_ICONS_EXTRA
            "CreateShortCut '$SMPROGRAMS\\\\Prosophor\\\\Prosophor.lnk' '$INSTDIR\\\\bin\\\\prosophor.exe'")
        set(CPACK_NSIS_DELETE_ICONS_EXTRA
            "Delete '$SMPROGRAMS\\\\Prosophor\\\\Prosophor.lnk'")
        set(CPACK_NSIS_INSTALLED_FILES_DISTRIBUTION OFF)
    else()
        message(STATUS "NSIS not found, using ZIP package")
        set(CPACK_GENERATOR "ZIP")
    endif()
endif()

include(CPack)
