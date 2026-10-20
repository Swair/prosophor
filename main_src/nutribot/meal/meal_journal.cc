// MealJournal 实现 —— 就餐会话累计与交接（详见头注释）
#include "meal/meal_journal.h"

namespace prosophor::nr::app {

void MealJournal::BeginMeal(prosophor::SteadyClock::TimePoint now) {
    if (active_) return;   // 同一次用餐只记一遍
    active_ = true;
    started_at_ = now;
    summary_ = MealSummary{};
    seen_dishes_.clear();
    seen_reminder_keys_.clear();
}

void MealJournal::Observe(const prosophor::nr::domain::ScenePackage& pkg) {
    if (!active_) return;
    ++summary_.perception_rounds;

    for (const auto& food : pkg.foods) {
        if (!seen_dishes_.insert(food.name).second) continue;   // 去重：取首次营养值
        summary_.dishes.push_back(food.name);
        if (food.has_nutrition) {
            summary_.total_energy_kcal += food.nutrition.energy_kcal;
            ++summary_.dishes_with_nutrition;
        } else {
            ++summary_.dishes_without_nutrition;
        }
    }

    for (const auto& event : pkg.events) {
        const std::string key = event.member_name + "|" + event.kind + "|" + event.food;
        if (!seen_reminder_keys_.insert(key).second) continue;
        if (summary_.reminders.size() < max_reminders) {
            summary_.reminders.push_back(event);
        }
    }
}

std::optional<MealSummary> MealJournal::EndMeal(
    prosophor::SteadyClock::TimePoint now) {
    if (!active_) return std::nullopt;
    active_ = false;

    summary_.duration_seconds = static_cast<int>(
        prosophor::SteadyClock::ToMillis(now - started_at_) / 1000);

    MealSummary finished = std::move(summary_);
    summary_ = MealSummary{};
    seen_dishes_.clear();
    seen_reminder_keys_.clear();
    return finished;
}

}  // namespace prosophor::nr::app
