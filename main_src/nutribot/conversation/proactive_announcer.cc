// ProactiveAnnouncer 实现 —— 主动播报去重/节流/组话术（纯逻辑）
#include "conversation/proactive_announcer.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace prosophor::nr::app {

namespace {

// 报菜名时说多少道（超过则"…等 N 道菜"）——控制单句时长与 120 字截断余量
constexpr size_t max_reported_dishes = 6;

// 菜品名以中文顿号连接（保持排序后的稳定输出）；超过 limit 收尾"…等 N 道菜"
std::string JoinFoodNames(const std::set<std::string>& food_names,
                          size_t limit = static_cast<size_t>(-1)) {
    std::string out;
    size_t shown = 0;
    for (const auto& name : food_names) {
        if (shown >= limit) {
            out += "…等 " + std::to_string(food_names.size()) + " 道菜";
            break;
        }
        if (!out.empty()) out += "、";
        out += name;
        ++shown;
    }
    return out;
}

// 热量取整到 10（"约" 语义下的精度上限；逐卡话术是假精确）
std::string RoundedKcal(double kcal) {
    return std::to_string(static_cast<int>(std::lround(kcal / 10.0) * 10));
}

// 时长话术："用了 N 分钟"，不足一分钟不装作有分钟数
std::string DurationPhrase(int seconds) {
    const int minutes = seconds / 60;
    if (minutes < 1) return "用了不到一分钟";
    return "用了 " + std::to_string(minutes) + " 分钟";
}

// 报菜名文案：菜名句在前、热量句在后 —— SanitizeForSpeech 超限截断到句末时
// 先丢热量、保住菜名（信息优先级）；未命中知识库的菜诚实计数不编数。
std::string ComposeFoodAnnouncement(
    const std::vector<prosophor::nr::domain::FoodItem>& foods) {
    std::set<std::string> food_names;
    int with_nutrition = 0;
    double total_kcal = 0.0;
    for (const auto& food : foods) {
        food_names.insert(food.name);
        if (food.has_nutrition) {
            ++with_nutrition;
            total_kcal += food.nutrition.energy_kcal;
        }
    }

    std::string text = "桌上现在有：" + JoinFoodNames(food_names, max_reported_dishes);
    if (with_nutrition > 0) {
        text += "，合计约 " + RoundedKcal(total_kcal) + " 千卡";
        const int unmatched = static_cast<int>(foods.size()) - with_nutrition;
        if (unmatched > 0) {
            text += "（另有 " + std::to_string(unmatched) + " 道我没见过）";
        }
    }
    text += "。";
    return text;
}

// 餐后小结文案：时长/菜数/热量在前，提醒回顾最后（超长先丢回顾）；
// 没看见菜时直说，不编菜品不编热量。
std::string ComposeMealSummary(const MealSummary& summary) {
    if (summary.perception_rounds == 0 || summary.dishes.empty()) {
        return "这顿饭" + DurationPhrase(summary.duration_seconds) +
               "，不过我没看清桌上有什么菜。";
    }

    std::string text = "这顿饭" + DurationPhrase(summary.duration_seconds) +
                       "，一共 " + std::to_string(summary.dishes.size()) + " 道菜：" +
                       JoinFoodNames(std::set<std::string>(summary.dishes.begin(),
                                                           summary.dishes.end()),
                                     max_reported_dishes);
    if (summary.dishes_with_nutrition > 0) {
        text += "，合计约 " + RoundedKcal(summary.total_energy_kcal) + " 千卡";
        if (summary.dishes_without_nutrition > 0) {
            text += "（另有 " + std::to_string(summary.dishes_without_nutrition) +
                    " 道我没见过）";
        }
    }
    text += "。";
    if (!summary.reminders.empty()) {
        text += "刚才提醒过：";
        for (size_t i = 0; i < summary.reminders.size(); ++i) {
            if (i > 0) text += "；";
            text += summary.reminders[i].message;
        }
    }
    return text;
}


// 挂起提醒组一句话：多条以分号连接（一次播完，不逐条排队）；
// spoken_count 记下真正贡献了话术的条数（空话术的异常事件不计数）
std::string JoinReminderMessages(
    const std::deque<prosophor::nr::domain::ReminderEvent>& reminders,
    int& spoken_count) {
    std::string out;
    spoken_count = 0;
    for (const auto& event : reminders) {
        if (event.message.empty()) continue;
        if (!out.empty()) out += "；";
        out += event.message;
        ++spoken_count;
    }
    return out;
}

}  // namespace

std::string FoodSetKey(const std::vector<prosophor::nr::domain::FoodItem>& foods) {
    std::set<std::string> names;
    for (const auto& food : foods) names.insert(food.name);
    std::string key;
    for (const auto& name : names) {
        if (!key.empty()) key += "\n";
        key += name;
    }
    return key;
}

ProactiveAnnouncer::ProactiveAnnouncer(AnnouncerTuning tuning)
    : tuning_(tuning) {}

bool ProactiveAnnouncer::GapElapsed(Clock::TimePoint now, int gap_ms) const {
    if (!last_spoke_at_.has_value()) return true;
    return Clock::ToMillis(now - *last_spoke_at_) >= gap_ms;
}

void ProactiveAnnouncer::NoteSpoken(Clock::TimePoint now) {
    last_spoke_at_ = now;
}

void ProactiveAnnouncer::NoteConversationSpeech(Clock::TimePoint now) {
    NoteSpoken(now);
}

void ProactiveAnnouncer::DropVanished(const std::set<std::string>& food_names) {
    for (auto it = pending_reminders_.begin(); it != pending_reminders_.end();) {
        if (food_names.count(it->food) == 0) {
            it = pending_reminders_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = notified_.begin(); it != notified_.end();) {
        if (food_names.count(it->second) == 0) {
            it = notified_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string ProactiveAnnouncer::PlanPerception(
    const prosophor::nr::domain::ScenePackage& pkg, Clock::TimePoint now,
    bool allow_speech) {
    // 1) 稳定性判定：桌上状态只在连续两轮集合一致时采纳 —— 单次抖动不改登记
    const std::string food_key = FoodSetKey(pkg.foods);
    const bool settled = (food_key == prev_food_key_);
    prev_food_key_ = food_key;

    if (settled) {
        settled_food_key_ = food_key;
        std::set<std::string> food_names;
        for (const auto& food : pkg.foods) food_names.insert(food.name);
        DropVanished(food_names);
        if (pkg.foods.empty()) {
            announced_food_key_.clear();   // 撤桌静默记账：下一餐上菜重新报菜名
        }
    }
    // 提醒**不等两轮稳定**：禁忌/过敏是安全项，看到即排队（重复由 notified_ 去重、
    // 撤桌由 DropVanished 作废）—— 代价是 VL 单轮抖动会出一条误报，用误触发率量化
    for (const auto& event : pkg.events) {
        if (notified_.insert({event.member_name, event.food}).second) {
            pending_reminders_.push_back(event);
        }
    }

    // 2) 有话可说吗：挂起提醒，或"已采纳且变化过"的菜品集合
    const bool foods_changed =
        settled && !pkg.foods.empty() && settled_food_key_ != announced_food_key_;
    if (pending_reminders_.empty() && !foods_changed) {
        stats_.pending_reminders = static_cast<int>(pending_reminders_.size());
        return {};
    }

    // 2.5) 静默期：登记照做（上面已完成），话术压住不产出。挂起提醒原样保留，
    //      场景/访客状态恢复后走正常节流窗口补播；报菜名的 announced_food_key_
    //      不推进，恢复后仍能识别出"这是没播过的新集合"。
    if (!allow_speech) {
        ++stats_.suppressed_by_scene;
        stats_.pending_reminders = static_cast<int>(pending_reminders_.size());
        return {};
    }

    // 3) 节流：挂起提醒不丢，等窗口过去补播。警告级用短窗（warn_gap_ms）就近播，
    //    一般播报（报菜名/小结）用 10s 长窗
    const bool has_warn = std::any_of(
        pending_reminders_.begin(), pending_reminders_.end(),
        [](const prosophor::nr::domain::ReminderEvent& e) { return e.level == "warn"; });
    if (!GapElapsed(now, has_warn ? tuning_.warn_gap_ms : tuning_.min_gap_ms)) {
        ++stats_.suppressed_by_gap;
        stats_.pending_reminders = static_cast<int>(pending_reminders_.size());
        return {};
    }

    // 4) 提醒优先：一条话说完全部挂起提醒
    if (!pending_reminders_.empty()) {
        int spoken_count = 0;
        const std::string text = JoinReminderMessages(pending_reminders_, spoken_count);
        pending_reminders_.clear();
        stats_.pending_reminders = 0;
        if (spoken_count == 0) return {};   // 事件无话术（规则引擎异常产物）：清掉不作声
        stats_.reminders_spoken += spoken_count;
        announced_food_key_ = food_key;   // 提醒已带菜名语境，本轮/本轮稳定后都不再单独报菜名
        NoteSpoken(now);
        return text;
    }

    // 5) 仅报菜名（带热量：项目书 Phase 1"说出菜名和大致热量"）
    announced_food_key_ = settled_food_key_;
    ++stats_.food_announcements;
    NoteSpoken(now);
    stats_.pending_reminders = 0;
    return ComposeFoodAnnouncement(pkg.foods);
}

std::string ProactiveAnnouncer::PlanMealSummary(const MealSummary& summary,
                                                Clock::TimePoint now,
                                                const std::string& salutation) {
    // 与其它一般播报同一节流（10s 长窗）：刚回答完用户/刚报过菜时不抢麦
    if (!GapElapsed(now, tuning_.min_gap_ms)) {
        ++stats_.suppressed_by_gap;
        return {};
    }
    std::string text = ComposeMealSummary(summary);
    if (text.empty()) return {};
    if (!salutation.empty()) {
        text = salutation + "，" + text;
    }
    ++stats_.meal_summaries;
    NoteSpoken(now);
    return text;
}

std::string ProactiveAnnouncer::PlanMealEntry(const std::string& meal_name,
                                              Clock::TimePoint now,
                                              const std::string& salutation) {
    if (meal_name.empty()) return {};
    if (!GapElapsed(now, tuning_.min_gap_ms)) {
        ++stats_.suppressed_by_gap;
        return {};
    }
    ++stats_.meal_announcements;
    NoteSpoken(now);
    const std::string who = salutation.empty() ? std::string() : salutation + "，";
    return who + "到" + meal_name + "时间啦，把菜端上桌，我帮你看看有没有要注意的。";
}

ProactiveAnnouncer::Stats ProactiveAnnouncer::stats() const {
    Stats snapshot = stats_;
    snapshot.pending_reminders = static_cast<int>(pending_reminders_.size());
    return snapshot;
}

}  // namespace prosophor::nr::app
