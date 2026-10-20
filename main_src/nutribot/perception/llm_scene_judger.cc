// LlmSceneJudger 实现 —— 语义对照 Python infrastructure/llm_scene_judger.py
#include "perception/llm_scene_judger.h"

#include <nlohmann/json.hpp>

namespace prosophor::nr::app {

using nlohmann::json;

namespace {

// 输出约束：JSON 单对象 + 枚举限定 + "不确定不猜，回当前状态"。
constexpr const char* kSceneJudgePrompt =
    "你是嵌入式营养机器人的餐桌场景判定器。你收到一份结构化观察 JSON，\n"
    "请判定当前处于哪个场景阶段，只输出一个 JSON 对象，不要任何其他文字。\n"
    "\n"
    "可选状态（严格枚举，只能取其一）：\n"
    "- 空桌待机：桌上无餐，无人用餐\n"
    "- 备餐中：有人/物在摆弄餐桌，菜刚上桌或在上菜，尚未开始吃\n"
    "- 用餐中：成员在席且桌上有菜（菜在减少/有进食动作）\n"
    "- 餐后：人已离席或菜已收，正在收桌/总结\n"
    "\n"
    "判定依据优先级：菜品证据 > 人脸/成员证据 > 运动档位 > 当前状态。\n"
    "输出格式：{\"state\": \"<枚举值>\", \"confidence\": 0~1 小数, "
    "\"reason\": \"<一句话依据>\"}\n"
    "\n"
    "规则（务必遵守）：\n"
    "1. state 必须是上述四个枚举值之一，禁止自创状态。\n"
    "2. 证据不足、无法判断时，state 输出\"当前状态\"字段的值（维持现状，不猜）。\n"
    "3. confidence < 0.6 时视为低置信，宁可维持现状。\n";

}  // namespace

std::string build_scene_judge_prompt(const SceneJudgeObservation& obs) {
    json payload;
    payload["current_state"] = obs.current_state;
    payload["members"] = obs.members;
    payload["foods"] = obs.foods;
    payload["motion_level"] = obs.motion_level;
    return std::string(kSceneJudgePrompt) + "\n观察: " + payload.dump();
}

std::optional<SceneJudgement> parse_scene_judge_response(
    const std::string& text) {
    if (text.empty()) return std::nullopt;
    try {
        // LLM 可能回带 ```json 围栏或夹带说明：取首个 {...} 整体
        const size_t b = text.find('{');
        const size_t e = text.rfind('}');
        if (b == std::string::npos || e == std::string::npos || e < b) {
            return std::nullopt;
        }
        const json data = json::parse(text.substr(b, e - b + 1));

        const auto state = nr::domain::scene_from_label(
            data.value("state", std::string()));
        if (!state.has_value()) return std::nullopt;   // 非法状态输出

        double conf = data.value("confidence", 0.0);
        conf = std::max(0.0, std::min(conf, 1.0));
        SceneJudgement j;
        j.state = *state;
        j.confidence = conf;
        j.reason = data.value("reason", std::string()).substr(0, 200);
        return j;
    } catch (...) {
        return std::nullopt;
    }
}

LlmSceneJudger::LlmSceneJudger(SceneJudgeBackend backend)
    : backend_(std::move(backend)) {
    available_ = static_cast<bool>(backend_);
}

std::optional<SceneJudgement> LlmSceneJudger::judge(
    const SceneJudgeObservation& observation) {
    if (!available_) return std::nullopt;
    try {
        const auto text = backend_(build_scene_judge_prompt(observation));
        if (!text.has_value()) return std::nullopt;
        return parse_scene_judge_response(*text);
    } catch (...) {
        return std::nullopt;   // 推理失败不影响主循环
    }
}

}  // namespace prosophor::nr::app
