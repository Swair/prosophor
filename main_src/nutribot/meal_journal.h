// 就餐会话累加器：从"进入用餐中"到"进入餐后"之间，累计这顿饭的账
//
// 纯逻辑、零 IO，可独立单测。消费方是餐后小结播报（ProactiveAnnouncer::
// PlanMealSummary）——它需要的是"这顿吃了什么、多久、提醒过谁"，而不是
// 场景包原始流。
//
// 语义约定：
//   - BeginMeal 时才开始记账；重复 Begin 不重置时钟（同一次用餐只记一遍）
//   - Observe 每轮感知喂一次：菜品按名去重（取首次出现的营养值）；提醒按
//     {成员,类型,菜品} 去重且只留前 max_reminders 条（小结回顾用，不求全）
//   - EndMeal 产出并清账；未 Begin 就 End 返回 nullopt（幂等，重复 End 同）
#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "events.h"

namespace prosophor::nr::app {

// 一顿饭的账（EndMeal 一次性交出）
struct MealSummary {
    int duration_seconds = 0;                     // "用餐中"→"餐后"的墙钟时长
    std::vector<std::string> dishes;              // 累计菜品（保序去重）
    double total_energy_kcal = 0.0;               // 只累有营养数据的菜（每份口径）
    int dishes_with_nutrition = 0;
    int dishes_without_nutrition = 0;             // 知识库没见过的菜数（诚实表述用）
    std::vector<prosophor::nr::domain::ReminderEvent> reminders;   // 回顾用（上限 3）
    int perception_rounds = 0;                    // 实际观察轮数（0 = 这顿没看见）
};

class MealJournal {
public:
    static constexpr size_t max_reminders = 3;

    // 进入"用餐中"时调用；已在记账中则忽略（不重置时钟）
    void BeginMeal(std::chrono::steady_clock::time_point now);
    // 每轮感知结果喂一次（仅在记账中生效）
    void Observe(const prosophor::nr::domain::ScenePackage& pkg);
    // 进入"餐后"时调用：产出这顿的账并清空；未 Begin 过返回 nullopt
    std::optional<MealSummary> EndMeal(std::chrono::steady_clock::time_point now);

    bool active() const { return active_; }
    // 诊断：当前累计菜品数
    size_t dish_count() const { return summary_.dishes.size(); }

private:
    bool active_ = false;
    std::chrono::steady_clock::time_point started_at_{};
    MealSummary summary_;
    std::set<std::string> seen_dishes_;
    std::set<std::string> seen_reminder_keys_;
};

}  // namespace prosophor::nr::app
