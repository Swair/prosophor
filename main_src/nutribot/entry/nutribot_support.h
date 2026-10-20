// nutribot_support：自检与常驻回路共用的小工具
//
// 仅放跨模块复用的纯工具/数据：推理心跳、脚本化对话端口、文案拼接、
// 营养文件定位、VL 后端适配。常驻/自检的具体编排不放这里。
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "config/config.h"
#include "domain/food.h"
#include "ports.h"
#include "perception/vl_food_detector.h"

namespace prosophor {
class LLMProvider;
}  // namespace prosophor

namespace prosophor::nr::nutribot {

// ── 推理心跳（watchdog 用）──
// 8B/QNN 实测偶发卡在 fastrpc 数分钟且 SDK 没有取消 API；心跳在每次推理
// 开始与结束各打一次，in_flight>0 才判超时。
extern std::atomic<int64_t> g_inference_heartbeat_ms;
extern std::atomic<int> g_inference_in_flight;

int64_t SteadyNowMs();

// RAII：异常路径也会把 in_flight 减回去，避免 watchdog 误判
struct InferenceHeartbeatGuard {
    InferenceHeartbeatGuard() {
        g_inference_heartbeat_ms.store(SteadyNowMs());
        g_inference_in_flight.fetch_add(1);
    }
    ~InferenceHeartbeatGuard() {
        g_inference_heartbeat_ms.store(SteadyNowMs());
        g_inference_in_flight.fetch_sub(1);
    }
};

// ── 脚本化对话端口（自检与常驻 seeding 共用）──
class ScriptedDialoguePort : public prosophor::nr::app::DialoguePort {
public:
    explicit ScriptedDialoguePort(std::vector<std::string> answers);

    std::optional<std::string> ask(const std::string& question) override;

private:
    std::deque<std::string> answers_;
};

// 注册案例：姓名 + 8 问脚本化回答（与单测 test_register_flow 同构）。
struct RegistrationCase {
    const char* name;
    std::vector<std::string> answers;
};

// ── 文案拼接 ──
std::string JoinNames(const std::set<std::string>& names);
std::string JoinFoodNames(const std::vector<prosophor::nr::domain::FoodItem>& foods);

// ── 路径 ──
std::string ResolveNutritionFile(const prosophor::NutribotConfig& cfg);

// ── VL 后端适配：图片 + 提问 -> 回复文本（带推理心跳）──
prosophor::nr::app::VisionQueryBackend MakeAidgenVisionBackend(
    std::shared_ptr<prosophor::LLMProvider> vl,
    int* model_calls,
    std::string* last_reply);

}  // namespace prosophor::nr::nutribot
