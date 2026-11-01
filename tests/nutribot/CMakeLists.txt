cmake_minimum_required(VERSION 3.16)
project(nutribot_tests LANGUAGES CXX)

# ============================================================================
# nutribot 单测（独立于主树构建）
#   被测代码与主树同一批源（main_src/nutribot/*.cc）本地收集，
#   零第三方：自写微框架 micro_test.hpp（不用 googletest）。
# 用法: cmake -S tests/nutribot -B <build_dir> && make && ./nr_tests
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 被测库源（与主树 GLOB 同集合）
# 仅测试用自包含库：只收集与引擎/语音解耦的领域与应用逻辑。
# 形态入口（nutribot_app/nutribot_support/resident_*）、设备工具
# （tools/）、抓帧线程（camera_source）依赖 AgentEngine/平台，均排除。
file(GLOB_RECURSE NUTRIBOT_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/nutribot/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/base/common/file_utils.cc
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/base/common/i18n.cc
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/base/voice_interaction/speech_text.cc
)
list(FILTER NUTRIBOT_TEST_SOURCES EXCLUDE REGEX
     "(nutribot_app|nutribot_selftest|nutribot_support|resident_.*)\.cc$")
list(FILTER NUTRIBOT_TEST_SOURCES EXCLUDE REGEX
     "(camera_source|nutribot_tools|robot_control_tools)\.cc$")
add_library(nr_test_core STATIC ${NUTRIBOT_TEST_SOURCES})
target_include_directories(nr_test_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/nutribot
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/base
)

add_executable(nr_tests
    test_main.cc
    test_member_repository.cc
    test_utterance_coalescer.cc
    test_meal_schedule.cc
    test_aidgen_tool_call_protocol.cc
)
find_package(spdlog CONFIG QUIET)
if(TARGET spdlog::spdlog)
    target_link_libraries(nr_test_core PUBLIC spdlog::spdlog)
endif()
target_link_libraries(nr_tests PRIVATE nr_test_core)
target_include_directories(nr_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                    # micro_test.hpp
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/prosophor_core   # providers/llm/prompt.h（仅头）
)

enable_testing()
add_test(NAME nr_tests COMMAND nr_tests)
