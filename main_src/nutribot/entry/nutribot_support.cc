// nutribot_support 实现。
#include "entry/nutribot_support.h"

#include <utility>

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "core/messages_schema.h"
#include "providers/llm/llm_provider.h"

namespace prosophor::nr::nutribot {

std::atomic<int64_t> g_inference_heartbeat_ms{0};
std::atomic<int> g_inference_in_flight{0};

int64_t SteadyNowMs() {
    return prosophor::SteadyClock::ToMillis(
        prosophor::SteadyClock::Now().time_since_epoch());
}

ScriptedDialoguePort::ScriptedDialoguePort(std::vector<std::string> answers)
    : answers_(answers.begin(), answers.end()) {}

std::optional<std::string> ScriptedDialoguePort::ask(const std::string& question) {
    LOG_INFO("    [问] {}", question.c_str());
    if (answers_.empty()) {
        LOG_INFO("    [答] (无回应)");
        return std::nullopt;
    }
    std::string answer = std::move(answers_.front());
    answers_.pop_front();
    LOG_INFO("    [答] {}", answer.c_str());
    return answer;
}

std::string JoinNames(const std::set<std::string>& names) {
    std::string out;
    for (const auto& n : names) {
        if (!out.empty()) out += "、";
        out += n;
    }
    return out;
}

std::string JoinFoodNames(const std::vector<prosophor::nr::domain::FoodItem>& foods) {
    std::string out;
    for (const auto& f : foods) {
        if (!out.empty()) out += "、";
        out += f.name;
    }
    return out;
}

std::string ResolveNutritionFile(const prosophor::NutribotConfig& cfg) {
    if (!cfg.nutrition_file.empty()) {
        return prosophor::ExpandHome(cfg.nutrition_file);
    }
    const std::string base = prosophor::JoinPath(
        prosophor::ProsophorConfig::BaseDir().string(), "nutribot_dishes.json");
    if (prosophor::FileExists(base)) return base;
    return "config/.prosophor/nutribot_dishes.json";
}

prosophor::nr::app::VisionQueryBackend MakeAidgenVisionBackend(
    std::shared_ptr<prosophor::LLMProvider> vl, int* model_calls, std::string* last_reply) {
    return [vl, model_calls, last_reply](const std::string& image_path,
                                         const std::string& question)
               -> std::optional<std::string> {
        prosophor::ChatRequest req;
        prosophor::MessageSchema user("user", question);
        user.AddImageContent(image_path);
        req.messages.push_back(std::move(user));
        req.max_tokens = 128;   // 识别输出只是短 JSON 数组：限尾防小模型放飞（默认 8192）
        if (model_calls != nullptr) ++*model_calls;
        if (last_reply != nullptr) last_reply->clear();   // 失败时不留上一轮的旧原话
        // 心跳只包 SDK 调用：prompt 组装不含推理耗时
        const auto resp = [&req, vl] {
            InferenceHeartbeatGuard heartbeat;
            return vl->Chat(req);
        }();
        if (!resp.error_msg.empty()) {
            LOG_INFO("  [后端错误] {}", resp.error_msg.c_str());
            return std::nullopt;
        }
        if (last_reply != nullptr) *last_reply = resp.content_text;
        return resp.content_text;
    };
}

}  // namespace prosophor::nr::nutribot
