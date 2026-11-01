// nutribot_support 实现。
#include "entry/nutribot_support.h"

#include <ctime>
#include <thread>

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "prosophor_core/public/agent_engine.h"

namespace prosophor::nr::nutribot {

namespace {

constexpr int vision_poll_interval_ms = 200;
constexpr int vision_wait_timeout_ms = 180000;
constexpr int vision_log_interval_ms = 10000;

}  // namespace

VisionWaitResult WaitForAidgenVision(prosophor::AgentEngine& engine,
                                     const std::function<bool()>& abort,
                                     const std::string& log_tag) {
    VisionWaitResult result;
    auto status = engine.GetProviderStatus("aidgen");
    while (status.loading && result.waited_ms < vision_wait_timeout_ms) {
        if (abort && abort()) {
            result.aborted = true;
            return result;
        }
        if (result.waited_ms % vision_log_interval_ms == 0) {
            LOG_INFO("{}: 等待 aidgen 模型装载…（{}s）", log_tag,
                     result.waited_ms / 1000);
        }
        std::this_thread::sleep_for(Milliseconds(vision_poll_interval_ms));
        result.waited_ms += vision_poll_interval_ms;
        status = engine.GetProviderStatus("aidgen");
    }

    if (!status.loaded || !status.has_vision) {
        result.error = status.loaded ? "无视觉配置" : "装载失败/超时";
        if (!status.error.empty()) result.error += "：" + status.error;
        return result;
    }
    result.ready = true;
    return result;
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

int LocalMinutesOfDay() {
    const std::time_t wallclock = std::time(nullptr);
    std::tm local{};
    localtime_r(&wallclock, &local);
    return local.tm_hour * 60 + local.tm_min;
}

}  // namespace prosophor::nr::nutribot
