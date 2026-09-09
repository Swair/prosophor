// LlmSceneJudger 测试：prompt 构建 / 回复解析 / Fake 后端全链路
#include <string>

#include "llm_scene_judger.h"
#include "micro_test.hpp"
#include "ports.h"
#include "scene.h"

using namespace prosophor::nr::app;
using prosophor::nr::domain::SceneState;

namespace {

SceneJudgeObservation make_obs() {
    SceneJudgeObservation o;
    o.current_state = "备餐中";
    o.members = {"小明"};
    o.foods = {"宫保鸡丁", "米饭"};
    o.motion_level = "high";
    return o;
}

}  // namespace

// ---- prompt 构建 ----
TEST(judger, prompt_contains_observation) {
    const std::string p = build_scene_judge_prompt(make_obs());
    EXPECT_TRUE(p.find("备餐中") != std::string::npos);
    EXPECT_TRUE(p.find("小明") != std::string::npos);
    EXPECT_TRUE(p.find("宫保鸡丁") != std::string::npos);
    EXPECT_TRUE(p.find("\"motion_level\": \"high\"") != std::string::npos ||
                p.find("high") != std::string::npos);
}

// ---- 回复解析 ----
TEST(judger, parse_plain_json) {
    const auto r = parse_scene_judge_response(
        R"({"state": "用餐中", "confidence": 0.85, "reason": "成员在席且有菜"})");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->state == SceneState::DINING);
    EXPECT_NEAR(r->confidence, 0.85, 1e-9);
    EXPECT_EQ(r->reason, "成员在席且有菜");
}

TEST(judger, parse_with_json_fence) {
    const auto r = parse_scene_judge_response(
        "好的，分析如下：\n```json\n{\"state\": \"空桌待机\", "
        "\"confidence\": 0.7}\n```\n");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->state == SceneState::IDLE);
}

TEST(judger, parse_chinese_enum_alias) {
    // 别名 "空桌" 归一为 IDLE（scene_from_label 别名兜底）
    const auto r = parse_scene_judge_response(R"({"state": "空桌"})");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->state == SceneState::IDLE);
}

TEST(judger, parse_invalid_returns_null) {
    EXPECT_FALSE(parse_scene_judge_response("").has_value());
    EXPECT_FALSE(parse_scene_judge_response("没有JSON").has_value());
    EXPECT_FALSE(parse_scene_judge_response(R"({"state": "乱写"})").has_value());
    EXPECT_FALSE(parse_scene_judge_response("这不是json").has_value());
}

TEST(judger, confidence_clamped) {
    const auto hi = parse_scene_judge_response(R"({"state": "用餐中", "confidence": 2.0})");
    EXPECT_TRUE(hi.has_value());
    EXPECT_NEAR(hi->confidence, 1.0, 1e-9);
    const auto lo = parse_scene_judge_response(R"({"state": "用餐中", "confidence": -1})");
    EXPECT_TRUE(lo.has_value());
    EXPECT_NEAR(lo->confidence, 0.0, 1e-9);
}

// ---- 全链路：Fake 后端 ----
TEST(judger, judge_with_backend) {
    bool called = false;
    LlmSceneJudger judger([&called](const std::string&) {
        called = true;
        return std::optional<std::string>(
            R"({"state": "用餐中", "confidence": 0.9, "reason": "fake"})");
    });
    const auto j = judger.judge(make_obs());
    EXPECT_TRUE(called);
    EXPECT_TRUE(j.has_value());
    EXPECT_TRUE(j->state == SceneState::DINING);
}

TEST(judger, judge_unavailable_returns_null) {
    LlmSceneJudger judger(nullptr);
    EXPECT_FALSE(judger.judge(make_obs()).has_value());
}

TEST(judger, judge_backend_nullopt_returns_null) {
    LlmSceneJudger judger([](const std::string&) {
        return std::optional<std::string>();
    });
    EXPECT_FALSE(judger.judge(make_obs()).has_value());
}

TEST(judger, judge_backend_throw_swallowed) {
    LlmSceneJudger judger([](const std::string&) -> std::optional<std::string> {
        throw std::runtime_error("NPU down");
    });
    EXPECT_FALSE(judger.judge(make_obs()).has_value());   // 不抛给主循环
}
