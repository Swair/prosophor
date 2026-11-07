// resident 餐点域：饭点问候 / 餐后小结 / 场景与餐账
#include "entry/resident_session.h"

#include <optional>
#include <string>
#include <utility>

#include "common/log_wrapper.h"
#include "entry/nutribot_support.h"

namespace prosophor::nr::nutribot {

namespace {

std::string MealSummaryEvent(const prosophor::nr::app::MealSummary& summary) {
    if (summary.perception_rounds == 0 || summary.dishes.empty()) {
        return "用餐结束。这顿没看清桌上有什么菜，请用一句话做个简短的餐后小结。";
    }
    const int minutes = summary.duration_seconds / 60;
    std::string event = "用餐结束。";
    event += minutes >= 1 ? "时长约 " + std::to_string(minutes) + " 分钟；"
                          : "时长不到一分钟；";
    event += "桌上出现过的菜：";
    for (size_t i = 0; i < summary.dishes.size(); ++i) {
        if (i > 0) event += "、";
        event += summary.dishes[i];
    }
    if (summary.dishes_with_nutrition > 0) {
        const int kcal =
            static_cast<int>(summary.total_energy_kcal / 10.0 + 0.5) * 10;
        event += "；合计约 " + std::to_string(kcal) + " 千卡";
    }
    if (summary.dishes_without_nutrition > 0) {
        event += "；另有 " + std::to_string(summary.dishes_without_nutrition) +
                 " 道菜没有营养数据";
    }
    event += "。请用一到两句话做餐后小结，包含菜名和大致热量，语气自然。";
    return event;
}


// 本地时间当日分钟数（饭点判定；localtime_r 线程安全）

}  // namespace

void ResidentSession::TickMealWindow(prosophor::SteadyClock::TimePoint now) {
    // ③ 饭点：进入窗口问候一次（节流中挂起，按感知节拍重试）；排在感知前 ——
    // 问候是瞬时的，不该排在一次秒级 VL 推理后面。问候承诺"我帮你看看"，
    // 故与感知同步启用（VL 装配失败时不问候）。
    // 饭点检测/挂起不受说话状态影响；开口只允许在待机且半双工允许时。
    const auto window = schedule_.Lookup(LocalMinutesOfDay());
    const std::string meal_now = window.has_value() ? window->name : std::string();
    if (meal_now != current_meal_name_) {
        current_meal_name_ = meal_now;
        if (!meal_now.empty()) {
            LOG_INFO("[饭点] 进入「{}」窗口", meal_now.c_str());
            pending_meal_greeting_ = meal_now;
        }
    }
    if (now < next_meal_retry_at_) return;

    // 无成员档案：问候挂在 pending 上等下轮（只是推迟，不丢）；
    // 小结没有重播时机，放弃并计数（下次"餐后"会有新账）
    if (no_member_profiles_) {
        if (pending_meal_greeting_.has_value() && config_.meal.announce) {
            ++silence_skipped_;
            next_meal_retry_at_ = now + perception_period_;
        }
        if (pending_meal_summary_.has_value()) {
            ++silence_skipped_;
            pending_meal_summary_.reset();
            next_meal_retry_at_ = now + perception_period_;
        }
        return;
    }

    // 已唤醒的对话窗口里不主动插话；回待机后再补播
    if (!CanStartProactiveSpeech()) return;

    // 饭点问候：交给 agent 任务事件，本地只给事实描述
    if (pending_meal_greeting_.has_value() && config_.meal.announce) {
        next_meal_retry_at_ = now + perception_period_;
        const std::string event = "进入" + *pending_meal_greeting_ +
                                  "窗口。你还没看到桌上的菜，只向在场成员问好、"
                                  "提醒把菜端上桌，不要评价菜品或营养。";
        const AgentTurnResult turn = RunAgentTurn("", "", event);
        if (turn.ok) {
            pending_meal_greeting_.reset();   // 模型决定沉默也算处理过
            if (SpeakAgentReply(turn.reply, /*proactive=*/true)) {
                return;   // 已进入播报，小结下轮再说
            }
        }
        // 调用失败：保持 pending，下轮重试
    }
    // 餐后小结：账本事实交给 agent 任务事件组话
    if (pending_meal_summary_.has_value() && now >= next_meal_retry_at_) {
        next_meal_retry_at_ = now + perception_period_;
        const AgentTurnResult turn =
            RunAgentTurn("", "", MealSummaryEvent(*pending_meal_summary_));
        if (turn.ok) {
            pending_meal_summary_.reset();
            SpeakAgentReply(turn.reply, /*proactive=*/true);
        }
    }
}

void ResidentSession::ApplySceneLabel(const std::string& label,
                                      prosophor::SteadyClock::TimePoint now) {
    if (label.empty() || label == current_scene_label_) return;
    const std::string previous = current_scene_label_;
    current_scene_label_ = label;
    LOG_INFO("[场景] {} → {}", previous.empty() ? "（启动）" : previous.c_str(),
             label.c_str());
    if (label == "用餐中") {
        journal_.BeginMeal(now);
    } else if (label == "餐后") {
        if (auto summary = journal_.EndMeal(now);
            summary.has_value() && config_.meal.announce) {
            pending_meal_summary_ = std::move(summary);
        }
    }
}

}  // namespace prosophor::nr::nutribot
