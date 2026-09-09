// 领域值对象：场景状态机（移植自 Python domain/scene.py，语义 1:1）
//
// 状态流转：空桌待机 -> 备餐中 -> 用餐中 -> 餐后 -> 待机；
// 状态决定感知策略（1fps 低功耗 / 感知全开 / LLM 生成小结）。
//
// 本文件纯逻辑、零第三方依赖，可独立单元测试。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <array>

namespace prosophor::nr::domain {

// 场景阶段枚举（与 Python SceneState 一一对应）
enum class SceneState {
    IDLE = 0,        // 空桌待机
    PREPARING = 1,   // 备餐中
    DINING = 2,      // 用餐中
    AFTER_MEAL = 3,  // 餐后
    UNKNOWN = 4      // 未知
};

// 中文标签（TTS/日志消费）；UNKNOWN -> "未知"
std::string_view scene_label(SceneState s);
// 由中文标签/英文名解析回枚举（判决结果归一化用）；失败返回 std::nullopt
std::optional<SceneState> scene_from_label(std::string_view label);

// 状态机的输入观察量（由感知层每帧计算）
struct SceneObservation {
    double motion_ratio = 0.0;  // 帧间差异占比（0-1）
    bool   has_faces = false;   // 是否检测到人脸
};

// 场景状态机（纯逻辑，可单测）
//
// 状态转移：
//   IDLE --运动持续 HOLD_MOTION--> PREPARING --人脸持续 HOLD_FACES--> DINING
//   DINING --静止+无人脸持续 HOLD_QUIET--> AFTER_MEAL --持续 HOLD_EMPTY--> IDLE
//   PREPARING --静止回落--> IDLE（虚惊一场）
// 转移用连续帧计数过滤抖动；停留在当前状态时保持最新观察值。
class SceneStateMachine {
public:
    // 转移阈值（帧数）与运动阈值 —— 与 Python 常量一致
    static constexpr int HOLD_MOTION = 10;
    static constexpr int HOLD_FACES = 20;
    static constexpr int HOLD_QUIET = 30;
    static constexpr int HOLD_EMPTY = 60;
    static constexpr double MOTION_LOW = 0.01;   // "静止" 运动阈值
    static constexpr double MOTION_HIGH = 0.05;  // "有动静" 运动阈值

    SceneStateMachine() { reset(); }

    SceneState state() const { return state_; }
    std::string_view label() const { return scene_label(state_); }
    // 本次会话转移次数（供 Agent 层统计）
    int transitions() const { return transitions_; }

    // 按观察量推进一次状态机；状态变化时返回新状态，否则返回当前状态
    SceneState update(const SceneObservation& obs);

    void reset();
    // 外部判决（如 LLM 复核）强制切换状态（幂等；UNKNOWN 忽略）。
    // 仅用于"判决侧确认"后同步本状态机；正常路径应走 update()。
    void force(SceneState s);

private:
    // 状态转移 + 计数清零防叠加
    void change(SceneState s);

    SceneState state_ = SceneState::IDLE;
    int transitions_ = 0;
    // 各目标状态的连续帧计数（与 Python 的计数器字典同语义）
    std::array<int, 4> counters_ = {0, 0, 0, 0};
};

// 判决侧迟滞投票窗：候选状态须连续一致 N 次才切换。
//
// 与 SceneStateMachine（输入侧连续帧计数）互补：状态机消抖作用于"观察输入"，
// 本投票窗把同样的迟滞思想搬到"判决输出"，吸收概率判据（LLM）的抖动。
// FSM 仍是默认权威——它自行转移时上层应调 resync() 同步并清票。
class StateVoter {
public:
    explicit StateVoter(int agree_window = 3,
                        SceneState current = SceneState::IDLE);

    int agree_window() const { return agree_window_; }
    int votes() const { return votes_; }              // 已累积一致票数
    SceneState current() const { return current_; }

    // FSM 已自行转移 -> 信任 FSM，跟随其状态并清空累计票
    void resync(SceneState s);

    // 投一票，返回（可能切换后的）当前状态。
    // candidate 为空/UNKNOWN/==当前 -> 无效票，清零（无证据不累积）
    SceneState vote(std::optional<SceneState> candidate);

private:
    int agree_window_;
    SceneState current_;
    int votes_ = 0;
};

}  // namespace prosophor::nr::domain
