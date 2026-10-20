// 场景状态 LLM 复核（Port: SceneJudgingPort）—— 移植自 Python
// infrastructure/llm_scene_judger.py（Phase 5 骨架）。
//
// 思路：状态机(确定性、免费)为默认权威；LLM 只做低频语义复核：
//   judge(结构化观察) -> 限定枚举候选状态，经应用层 StateVoter 连续确认才覆盖。
// 实现不可用/失败一律返回 nullopt（MealMonitor 自动回退纯状态机）。
//
// 执行后端以 std::function 注入（同步单轮文本生成）：
//   生产：包装 prosophor_core 的 AidGenProvider（ChatRequest 文本问答）
//   测试：Fake 直接返回预设文本
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "ports.h"
#include "domain/scene.h"

namespace prosophor::nr::app {

// 单轮文本生成后端：prompt -> 回复文本；不可用返回 nullopt
using SceneJudgeBackend =
    std::function<std::optional<std::string>(const std::string& prompt)>;

// 由结构化观察组装判定 prompt（纯函数，可单测）
std::string build_scene_judge_prompt(const SceneJudgeObservation& observation);

// 解析 LLM 回复为 SceneJudgement；JSON 非法/状态非法/缺失 -> nullopt
std::optional<SceneJudgement> parse_scene_judge_response(
    const std::string& text);

// LLM 场景复核实现（不可用时 judge 恒返回 nullopt）
class LlmSceneJudger : public SceneJudgingPort {
public:
    explicit LlmSceneJudger(SceneJudgeBackend backend);

    std::optional<SceneJudgement> judge(
        const SceneJudgeObservation& observation) override;
    void release() override {}

private:
    SceneJudgeBackend backend_;
    bool available_ = false;
};

}  // namespace prosophor::nr::app
