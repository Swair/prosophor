// 状态机 + 投票窗单元测试（对照 Python domain 语义逐条移植）
#include <ostream>

#include "scene.h"
#include "micro_test.hpp"

using namespace prosophor::nr::domain;

// 仅测试用：EXPECT_EQ 失败信息需要可打印的枚举（ADL，不污染领域层）
namespace prosophor::nr::domain {
inline std::ostream& operator<<(std::ostream& os, SceneState s) {
    return os << scene_label(s);
}
}  // namespace prosophor::nr::domain

namespace {

// 工具：连续 N 帧喂相同观察，返回最终状态
SceneState drive(SceneStateMachine& sm, const SceneObservation& obs, int frames) {
    for (int i = 0; i < frames; ++i) sm.update(obs);
    return sm.state();
}

SceneObservation still_no_face()  { return {0.0, false}; }         // 全静止
SceneObservation moving()         { return {0.10, false}; }        // 高运动
SceneObservation moving_with_face(){ return {0.10, true}; }
SceneObservation face_only()      { return {0.0, true}; }          // 静止+人脸

}  // namespace

// ---- SceneStateMachine: 主路径 ----
TEST(fsm, idle_to_preparing_after_hold_motion) {
    SceneStateMachine sm;
    EXPECT_EQ(drive(sm, moving(), SceneStateMachine::HOLD_MOTION - 1),
              SceneState::IDLE);
    EXPECT_EQ(drive(sm, moving(), 1), SceneState::PREPARING);
}

TEST(fsm, preparing_to_dining_after_hold_faces) {
    SceneStateMachine sm;
    drive(sm, moving(), SceneStateMachine::HOLD_MOTION);  // -> PREPARING
    EXPECT_EQ(drive(sm, face_only(), SceneStateMachine::HOLD_FACES - 1),
              SceneState::PREPARING);
    EXPECT_EQ(drive(sm, face_only(), 1), SceneState::DINING);
}

TEST(fsm, dining_to_after_meal_after_quiet) {
    SceneStateMachine sm;
    drive(sm, moving(), SceneStateMachine::HOLD_MOTION);          // PREPARING
    drive(sm, face_only(), SceneStateMachine::HOLD_FACES);        // DINING
    EXPECT_EQ(drive(sm, still_no_face(), SceneStateMachine::HOLD_QUIET - 1),
              SceneState::DINING);
    EXPECT_EQ(drive(sm, still_no_face(), 1), SceneState::AFTER_MEAL);
}

TEST(fsm, after_meal_to_idle_after_empty) {
    SceneStateMachine sm;
    drive(sm, moving(), SceneStateMachine::HOLD_MOTION);
    drive(sm, face_only(), SceneStateMachine::HOLD_FACES);
    drive(sm, still_no_face(), SceneStateMachine::HOLD_QUIET);   // AFTER_MEAL
    EXPECT_EQ(drive(sm, still_no_face(), SceneStateMachine::HOLD_EMPTY - 1),
              SceneState::AFTER_MEAL);
    EXPECT_EQ(drive(sm, still_no_face(), 1), SceneState::IDLE);
}

// ---- 抖动消抖 / 回退 ----
TEST(fsm, preparing_false_alarm_returns_idle) {
    SceneStateMachine sm;
    drive(sm, moving(), SceneStateMachine::HOLD_MOTION);   // PREPARING
    EXPECT_EQ(sm.state(), SceneState::PREPARING);
    // 动静平息且无人，持续低运动帧 -> 回落 IDLE（虚惊一场）
    EXPECT_EQ(drive(sm, still_no_face(), SceneStateMachine::HOLD_MOTION - 1),
              SceneState::PREPARING);
    EXPECT_EQ(drive(sm, still_no_face(), 1), SceneState::IDLE);
}

TEST(fsm, interrupted_motion_resets_counter) {
    SceneStateMachine sm;
    drive(sm, moving(), 5);
    drive(sm, still_no_face(), 1);               // 打断
    EXPECT_EQ(drive(sm, moving(), SceneStateMachine::HOLD_MOTION - 1),
              SceneState::IDLE);                 // 需重新累积满 10 帧
}

TEST(fsm, dining_interruption_keeps_dining) {
    SceneStateMachine sm;
    drive(sm, moving(), SceneStateMachine::HOLD_MOTION);
    drive(sm, face_only(), SceneStateMachine::HOLD_FACES);  // DINING
    // 中途有动静（非静止）不算安静帧 -> 不推进 AFTER_MEAL
    EXPECT_EQ(drive(sm, moving_with_face(), 100), SceneState::DINING);
}

// ---- 边界值：运动阈值 ----
TEST(fsm, motion_threshold_boundaries) {
    SceneStateMachine sm;
    // MOTION_HIGH 是"有动静"下界：恰好等于不算（Python: motion > HIGH 严格）
    drive(sm, {SceneStateMachine::MOTION_HIGH, false},
          SceneStateMachine::HOLD_MOTION + 5);
    EXPECT_EQ(sm.state(), SceneState::IDLE);
    // 略高于阈值：须重新连续满 HOLD_MOTION 帧才转移
    drive(sm, {SceneStateMachine::MOTION_HIGH + 1e-6, false},
          SceneStateMachine::HOLD_MOTION - 1);
    EXPECT_EQ(sm.state(), SceneState::IDLE);
    drive(sm, {SceneStateMachine::MOTION_HIGH + 1e-6, false}, 1);
    EXPECT_EQ(sm.state(), SceneState::PREPARING);
}

// ---- force / reset / transitions ----
TEST(fsm, force_switches_and_resets_counters) {
    SceneStateMachine sm;
    sm.force(SceneState::DINING);
    EXPECT_EQ(sm.state(), SceneState::DINING);
    EXPECT_EQ(sm.transitions(), 1);
    // 幂等：同态 force 不计数
    sm.force(SceneState::DINING);
    EXPECT_EQ(sm.transitions(), 1);
    // UNKNOWN 忽略
    sm.force(SceneState::UNKNOWN);
    EXPECT_EQ(sm.state(), SceneState::DINING);
}

TEST(fsm, reset_returns_idle) {
    SceneStateMachine sm;
    drive(sm, moving(), SceneStateMachine::HOLD_MOTION);
    sm.reset();
    EXPECT_EQ(sm.state(), SceneState::IDLE);
    EXPECT_EQ(sm.transitions(), 0);
}

TEST(fsm, chinese_labels) {
    EXPECT_EQ(scene_label(SceneState::IDLE), "空桌待机");
    EXPECT_EQ(scene_label(SceneState::PREPARING), "备餐中");
    EXPECT_EQ(scene_label(SceneState::DINING), "用餐中");
    EXPECT_EQ(scene_label(SceneState::AFTER_MEAL), "餐后");
    EXPECT_EQ(scene_label(SceneState::UNKNOWN), "未知");
}

TEST(fsm, from_label_normalization) {
    EXPECT_TRUE(scene_from_label("用餐中") == SceneState::DINING);
    EXPECT_TRUE(scene_from_label("DINING") == SceneState::DINING);
    EXPECT_TRUE(scene_from_label("空桌") == SceneState::IDLE);   // 别名
    EXPECT_TRUE(scene_from_label("餐后总结") == SceneState::AFTER_MEAL);
    EXPECT_TRUE(!scene_from_label("胡说八道").has_value());
}

// ---- StateVoter ----
TEST(voter, three_agree_votes_switch) {
    StateVoter v(3, SceneState::IDLE);
    EXPECT_EQ(v.vote(SceneState::DINING), SceneState::IDLE);   // 1/3
    EXPECT_EQ(v.vote(SceneState::DINING), SceneState::IDLE);   // 2/3
    EXPECT_EQ(v.vote(SceneState::DINING), SceneState::DINING); // 3/3 切换
    EXPECT_EQ(v.current(), SceneState::DINING);
}

// Python 语义核对（2026-09-08）：vote() 只区分"是否异于当前状态"，
// 不区分候选是谁——任意异见票都累加。docstring 写"连续一致 N 次"，
// 但实现与注释有出入；C++ 移植与 Python 行为 1:1 保持一致（回归基线可比）。
TEST(voter, differing_votes_accumulate_like_python) {
    StateVoter v(3, SceneState::IDLE);
    v.vote(SceneState::DINING);          // 异见 1/3
    EXPECT_EQ(v.votes(), 1);
    v.vote(SceneState::AFTER_MEAL);      // 异见 2/3（不同候选也累加 == Python 行为）
    EXPECT_EQ(v.votes(), 2);
    v.vote(SceneState::AFTER_MEAL);      // 3/3 -> 切换到最近票候选
    EXPECT_EQ(v.current(), SceneState::AFTER_MEAL);
    EXPECT_EQ(v.votes(), 0);
}

TEST(voter, invalid_vote_clears_no_evidence) {
    StateVoter v(3, SceneState::IDLE);
    v.vote(SceneState::DINING);          // 1/3
    v.vote(std::nullopt);                // 无证据 -> 清票
    EXPECT_EQ(v.votes(), 0);
    v.vote(SceneState::DINING);
    v.vote(SceneState::UNKNOWN);         // UNKNOWN 无效票
    EXPECT_EQ(v.votes(), 0);
    v.vote(SceneState::DINING);
    v.vote(SceneState::IDLE);            // 与当前一致 = 无效票
    EXPECT_EQ(v.votes(), 0);
}

TEST(voter, resync_follows_fsm) {
    StateVoter v(3, SceneState::IDLE);
    v.vote(SceneState::DINING);
    v.resync(SceneState::PREPARING);     // FSM 自行转移
    EXPECT_EQ(v.current(), SceneState::PREPARING);
    EXPECT_EQ(v.votes(), 0);
}

TEST(voter, window_requires_at_least_two) {
    bool threw = false;
    try {
        StateVoter v(1);
    } catch (...) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}
