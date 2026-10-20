#include "domain/scene.h"

#include <cassert>
#include <cmath>
#include <stdexcept>

namespace prosophor::nr::domain {

std::string_view scene_label(SceneState s) {
    switch (s) {
        case SceneState::IDLE:       return "空桌待机";
        case SceneState::PREPARING:  return "备餐中";
        case SceneState::DINING:     return "用餐中";
        case SceneState::AFTER_MEAL: return "餐后";
        case SceneState::UNKNOWN:    return "未知";
    }
    return "未知";
}

std::optional<SceneState> scene_from_label(std::string_view label) {
    // 英文枚举名 / 中文标签（与 Python _normalize_state 兜底别名一致）
    if (label == "IDLE") return SceneState::IDLE;
    if (label == "PREPARING") return SceneState::PREPARING;
    if (label == "DINING") return SceneState::DINING;
    if (label == "AFTER_MEAL") return SceneState::AFTER_MEAL;
    if (label == "空桌待机" || label == "空桌" || label == "空闲") return SceneState::IDLE;
    if (label == "备餐中" || label == "备餐") return SceneState::PREPARING;
    if (label == "用餐中" || label == "用餐") return SceneState::DINING;
    if (label == "餐后" || label == "餐后总结") return SceneState::AFTER_MEAL;
    return std::nullopt;
}

// 前四态 -> 计数器下标（UNKNOWN 永不计数）
static int counter_index(SceneState s) {
    switch (s) {
        case SceneState::IDLE:       return 0;
        case SceneState::PREPARING:  return 1;
        case SceneState::DINING:     return 2;
        case SceneState::AFTER_MEAL: return 3;
        default: assert(false); return 0;
    }
}

SceneHoldThresholds MakeHoldThresholds(int frame_interval_ms,
                                       const SceneTimingSeconds& timing) {
    const int interval = frame_interval_ms > 0 ? frame_interval_ms : 3000;
    const auto to_frames = [interval](double seconds) {
        const double frames = seconds * 1000.0 / interval;
        const int ceiled = static_cast<int>(std::ceil(frames - 1e-9));  // 恰好整数不下取
        return ceiled > 1 ? ceiled : 1;
    };
    SceneHoldThresholds thresholds;
    thresholds.hold_motion = to_frames(timing.motion);
    thresholds.hold_faces = to_frames(timing.faces);
    thresholds.hold_quiet = to_frames(timing.quiet);
    thresholds.hold_empty = to_frames(timing.empty);
    return thresholds;
}

SceneState SceneStateMachine::update(const SceneObservation& obs) {
    const double motion = obs.motion_ratio;
    const bool faces = obs.has_faces;
    const bool foods = obs.has_foods;

    switch (state_) {
        case SceneState::IDLE: {
            int& c = counters_[counter_index(SceneState::IDLE)];
            c = (motion > MOTION_HIGH) ? c + 1 : 0;
            if (c >= thresholds_.hold_motion) change(SceneState::PREPARING);
            break;
        }
        case SceneState::PREPARING: {
            // ★ 扩展：上齐了才开饭（has_foods 默认 true 时 ≡ 旧的 faces 条件）。
            // 修掉桩人脸下"空桌也进用餐"的毛病。
            if (faces && foods) {
                int& c = counters_[counter_index(SceneState::DINING)];
                c += 1;
                if (c >= thresholds_.hold_faces) change(SceneState::DINING);
            } else {
                counters_[counter_index(SceneState::DINING)] = 0;
                // 虚惊一场：动静平息且无人 -> 回落 IDLE（保持 Python 1:1）。
                // 注意由此产生的窗口：菜已上桌但 VL 还没识别出来时，这里会先
                // 回落 IDLE —— 装配层用"起餐瞬间立刻触发一轮看菜"把窗口压到
                // 一轮 VL 之内（见 nutribot_app 场景转移处），不在此处加料。
                int& c = counters_[counter_index(SceneState::IDLE)];
                c = (motion < MOTION_LOW) ? c + 1 : 0;
                if (c >= thresholds_.hold_motion) change(SceneState::IDLE);
            }
            break;
        }
        case SceneState::DINING: {
            // ★ 扩展：安静且（人走了 或 菜撤了）即止餐。has_foods 默认 true 时
            // ≡ 旧的 motion < LOW && !faces（Python 1:1 语义保持）。
            const bool quiet = motion < MOTION_LOW && (!faces || !foods);
            int& c = counters_[counter_index(SceneState::AFTER_MEAL)];
            c = quiet ? c + 1 : 0;
            if (c >= thresholds_.hold_quiet) change(SceneState::AFTER_MEAL);
            break;
        }
        case SceneState::AFTER_MEAL: {
            // ★ 同上：AFTER_MEAL 没有回 PREPARING 的出口，若此处条件不可达会
            // 死锁（桩人脸恒真 + 菜收走后 foods 恒假时全靠本扩展解套）
            const bool empty = motion < MOTION_LOW && (!faces || !foods);
            int& c = counters_[counter_index(SceneState::IDLE)];
            c = empty ? c + 1 : 0;
            if (c >= thresholds_.hold_empty) change(SceneState::IDLE);
            break;
        }
        case SceneState::UNKNOWN:
            break;  // 未知态不流转
    }
    return state_;
}

void SceneStateMachine::force(SceneState s) {
    if (s == SceneState::UNKNOWN || s == state_) return;
    change(s);
}

void SceneStateMachine::change(SceneState s) {
    state_ = s;
    ++transitions_;
    counters_ = {0, 0, 0, 0};  // 计数清零防叠加
}

void SceneStateMachine::reset() {
    state_ = SceneState::IDLE;
    transitions_ = 0;
    counters_ = {0, 0, 0, 0};
}

SceneState SceneTicker::observe(std::string_view frame_key, const SceneObservation& obs) {
    if (frame_key.empty()) return fsm_.update(obs);   // 无帧概念：每次都推进
    if (frame_key == last_frame_key_) return fsm_.state();   // 同帧：幂等短路
    last_frame_key_.assign(frame_key);
    return fsm_.update(obs);
}

void SceneTicker::reset() {
    fsm_.reset();
    last_frame_key_.clear();
}

StateVoter::StateVoter(int agree_window, SceneState current)
    : agree_window_(agree_window), current_(current) {
    if (agree_window < 2) {
        throw std::invalid_argument("投票窗至少 2 次才谈得上'连续一致'");
    }
}

void StateVoter::resync(SceneState s) {
    current_ = s;
    votes_ = 0;
}

SceneState StateVoter::vote(std::optional<SceneState> candidate) {
    if (!candidate.has_value() || *candidate == SceneState::UNKNOWN ||
        *candidate == current_) {
        votes_ = 0;  // 无效票清零：无证据不累积
        return current_;
    }
    if (++votes_ >= agree_window_) {
        current_ = *candidate;
        votes_ = 0;
    }
    return current_;
}

}  // namespace prosophor::nr::domain
