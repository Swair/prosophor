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
# 仅测试用自包含库：业务逻辑源（排除形态入口 nutribot_app.cc——它依赖
# AgentEngine，属主形态 exe 直编对象；领域/应用逻辑与引擎解耦可单测；
# camera_source.cc 引 platform 子进程且起真线程抓帧，同样排除）
file(GLOB_RECURSE NUTRIBOT_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/nutribot/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/base/common/file_utils.cc
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/base/common/i18n.cc
)
list(FILTER NUTRIBOT_TEST_SOURCES EXCLUDE REGEX
     "(nutribot_app|nutribot_selftest|nutribot_support|resident_session)\.cc$")
list(FILTER NUTRIBOT_TEST_SOURCES EXCLUDE REGEX "camera_source\.cc$")
list(FILTER NUTRIBOT_TEST_SOURCES EXCLUDE REGEX "scene_monitor\.cc$")
add_library(nr_test_core STATIC ${NUTRIBOT_TEST_SOURCES})
target_include_directories(nr_test_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/nutribot
    ${CMAKE_CURRENT_LIST_DIR}/../../main_src/base
)

add_executable(nr_tests
    test_main.cc
    test_scene.cc
    test_rule_engine.cc
    test_meal_monitor.cc
    test_profile_quiz.cc
    test_member_repository.cc
    test_scene_judger.cc
    test_vl_food_detector.cc
    test_register_flow.cc
    test_speech_rules.cc
    test_meal_schedule.cc
    test_meal_journal.cc
    test_dish_nutrition.cc
    test_proactive_announcer.cc
    test_vision_stub.cc
    test_diner_context.cc
    test_tool_call_protocol.cc
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
