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

// ---- has_foods 附加式扩展（默认 true = 旧语义；新增止餐信号）----

TEST(fsm_foods, default_has_foods_is_true) {
    const SceneObservation two_field = {0.1, true};   // 旧式两字段聚合
    EXPECT_TRUE(two_field.has_foods);
}

TEST(fsm_foods, idle_ignores_foods) {
    // 桌上摆着剩菜但无人动：不构成起餐（起餐只认动静）
    SceneStateMachine sm;
    const SceneObservation obs{0.10, false, /*has_foods=*/true};
    EXPECT_EQ(drive(sm, obs, SceneStateMachine::HOLD_MOTION - 1), SceneState::IDLE);
}

TEST(fsm_foods, preparing_needs_food_to_enter_dining) {
    SceneHoldThresholds small;
    small.hold_motion = 2;
    small.hold_faces = 5;
    SceneStateMachine fast(small);
    drive(fast, {0.10, false, false}, 2);            // 动静 -> PREPARING
    EXPECT_EQ(fast.state(), SceneState::PREPARING);
    // 有人脸但空桌且安静：虚惊一场回落 IDLE（空桌开不了饭）
    EXPECT_EQ(drive(fast, {0.0, true, /*has_foods=*/false}, 2),
              SceneState::IDLE);

    // 重新起备餐；菜在桌上且有人脸：静止也照常累积进 DINING
    drive(fast, {0.10, false, true}, 2);             // -> PREPARING
    EXPECT_EQ(fast.state(), SceneState::PREPARING);
    EXPECT_EQ(drive(fast, {0.0, true, /*has_foods=*/true}, 4),
              SceneState::PREPARING);                // 还差 1 帧
    EXPECT_EQ(drive(fast, {0.0, true, /*has_foods=*/true}, 1),
              SceneState::DINING);
}

TEST(fsm_foods, preparing_holds_when_food_on_table_nobody_moving) {
    // 有人有菜但静止：累积满 hold_faces 直接开饭（不被虚惊分支打断）
    SceneHoldThresholds small;
    small.hold_motion = 2;
    small.hold_faces = 3;
    SceneStateMachine sm(small);
    drive(sm, {0.10, true, true}, 2);                // 动静 -> PREPARING
    EXPECT_EQ(drive(sm, {0.0, true, true}, 3), SceneState::DINING);
}

TEST(fsm_foods, dining_ends_when_table_cleared_without_faces_signal) {
    // 桩人脸场景（has_faces 恒真）：撤桌 + 静止 = 止餐（旧条件在此死锁）
    SceneHoldThresholds small;
    small.hold_motion = 2;
    small.hold_faces = 2;
    small.hold_quiet = 4;
    small.hold_empty = 4;
    SceneStateMachine sm(small);
    drive(sm, {0.10, false, false}, 2);              // PREPARING
    drive(sm, {0.0, true, true}, 2);                 // DINING
    EXPECT_EQ(sm.state(), SceneState::DINING);

    // 有菜 + 静止 + 有人：保持用餐（防空桌误判反向）
    EXPECT_EQ(drive(sm, {0.0, true, true}, 50), SceneState::DINING);
    // 撤桌：安静且无菜 -> 餐后
    EXPECT_EQ(drive(sm, {0.0, true, false}, 4), SceneState::AFTER_MEAL);
    // 继续空桌 -> 空桌待机（桩下全周期可达，旧条件在此卡死）
    EXPECT_EQ(drive(sm, {0.0, true, false}, 4), SceneState::IDLE);
}

TEST(fsm_foods, old_semantics_unchanged_when_foods_unknown) {
    // has_foods 默认 true 时四条转移 == 旧式：用旧式两字段驱动全周期
    SceneStateMachine sm;
    drive(sm, moving(), SceneStateMachine::HOLD_MOTION);      // PREPARING
    drive(sm, face_only(), SceneStateMachine::HOLD_FACES);    // DINING
    drive(sm, still_no_face(), SceneStateMachine::HOLD_QUIET);// AFTER_MEAL
    EXPECT_EQ(drive(sm, still_no_face(), SceneStateMachine::HOLD_EMPTY),
              SceneState::IDLE);
}

// ---- 阈值折算（秒 -> 帧）----

TEST(fsm_thresholds, default_timing_at_three_seconds) {
    const SceneHoldThresholds t = MakeHoldThresholds(3000);
    EXPECT_EQ(t.hold_motion, 5);    // 15s
    EXPECT_EQ(t.hold_faces, 10);    // 30s
    EXPECT_EQ(t.hold_quiet, 30);    // 90s
    EXPECT_EQ(t.hold_empty, 60);    // 180s
}

TEST(fsm_thresholds, custom_timing_and_rounding) {
    const SceneHoldThresholds t = MakeHoldThresholds(1000, {15, 30, 90, 180});
    EXPECT_EQ(t.hold_motion, 15);
    EXPECT_EQ(t.hold_faces, 30);
    // 向上取整：10.5s/3s = 3.5 -> 4
    const SceneHoldThresholds r = MakeHoldThresholds(3000, {10.5, 1, 1, 1});
    EXPECT_EQ(r.hold_motion, 4);
    EXPECT_EQ(r.hold_faces, 1);     // 下限 1（0.x 秒 -> 1 帧）
}

TEST(fsm_thresholds, non_positive_interval_falls_back_to_three_seconds) {
    EXPECT_EQ(MakeHoldThresholds(0).hold_motion, 5);
    EXPECT_EQ(MakeHoldThresholds(-100).hold_faces, 10);
}

TEST(fsm_thresholds, injected_holds_scale) {
    SceneStateMachine fast(MakeHoldThresholds(3000));   // hold_motion=5
    EXPECT_EQ(drive(fast, moving(), 4), SceneState::IDLE);
    EXPECT_EQ(drive(fast, moving(), 1), SceneState::PREPARING);
}

// ---- SceneTicker：同帧键幂等（App 场景节拍 + MealMonitor 双驱动共存）----

TEST(scene_ticker, same_frame_key_advances_once) {
    SceneHoldThresholds small;
    small.hold_motion = 3;
    SceneTicker ticker(small);
    const SceneObservation moving_obs{0.10, true, true};

    for (int i = 0; i < 10; ++i) {
        ticker.observe("frame_000001", moving_obs);   // 同帧重复驱动
    }
    EXPECT_EQ(ticker.state(), SceneState::IDLE);      // 只算 1 帧，未到 3
    EXPECT_EQ(ticker.transitions(), 0);

    ticker.observe("frame_000002", moving_obs);
    ticker.observe("frame_000003", moving_obs);
    EXPECT_EQ(ticker.state(), SceneState::PREPARING); // 3 帧达标
}

TEST(scene_ticker, empty_key_always_advances) {
    SceneHoldThresholds small;
    small.hold_motion = 2;
    SceneTicker ticker(small);
    ticker.observe("", {0.10, true, true});
    ticker.observe("", {0.10, true, true});
    EXPECT_EQ(ticker.state(), SceneState::PREPARING);
}

TEST(scene_ticker, reset_clears_state_and_key) {
    SceneHoldThresholds small;
    small.hold_motion = 1;
    SceneTicker ticker(small);
    ticker.observe("f1", {0.10, true, true});
    EXPECT_EQ(ticker.state(), SceneState::PREPARING);
    ticker.reset();
    EXPECT_EQ(ticker.state(), SceneState::IDLE);
    ticker.observe("f1", {0.10, true, true});         // 同键但已 reset：应推进
    EXPECT_EQ(ticker.state(), SceneState::PREPARING);
}
