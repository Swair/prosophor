#include "scene.h"

#include <cassert>
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

SceneState SceneStateMachine::update(const SceneObservation& obs) {
    const double motion = obs.motion_ratio;
    const bool faces = obs.has_faces;

    switch (state_) {
        case SceneState::IDLE: {
            int& c = counters_[counter_index(SceneState::IDLE)];
            c = (motion > MOTION_HIGH) ? c + 1 : 0;
            if (c >= HOLD_MOTION) change(SceneState::PREPARING);
            break;
        }
        case SceneState::PREPARING: {
            if (faces) {
                // 有人脸：朝 DINING 累积
                int& c = counters_[counter_index(SceneState::DINING)];
                c += 1;
                if (c >= HOLD_FACES) change(SceneState::DINING);
            } else {
                counters_[counter_index(SceneState::DINING)] = 0;
                // 虚惊一场：动静平息且无人 -> 回落 IDLE
                int& c = counters_[counter_index(SceneState::IDLE)];
                c = (motion < MOTION_LOW) ? c + 1 : 0;
                if (c >= HOLD_MOTION) change(SceneState::IDLE);
            }
            break;
        }
        case SceneState::DINING: {
            const bool quiet = motion < MOTION_LOW && !faces;
            int& c = counters_[counter_index(SceneState::AFTER_MEAL)];
            c = quiet ? c + 1 : 0;
            if (c >= HOLD_QUIET) change(SceneState::AFTER_MEAL);
            break;
        }
        case SceneState::AFTER_MEAL: {
            const bool empty = motion < MOTION_LOW && !faces;
            int& c = counters_[counter_index(SceneState::IDLE)];
            c = empty ? c + 1 : 0;
            if (c >= HOLD_EMPTY) change(SceneState::IDLE);
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
