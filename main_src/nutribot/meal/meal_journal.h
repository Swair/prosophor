// 就餐会话累加器：从"进入用餐中"到"进入餐后"之间，累计这顿饭的账
//
// 纯逻辑、零 IO。消费方是餐后小结的任务事件（ResidentSession 把账本压成
// 事实描述，话术交给 LLM）。
//
// 语义约定：
//   - BeginMeal 时才开始记账；重复 Begin 不重置时钟（同一次用餐只记一遍）
//   - ObserveFoods 每轮 agent 上报喂一次：菜品按名去重（取首次出现的营养值）
//   - EndMeal 产出并清账；未 Begin 就 End 返回 nullopt（幂等，重复 End 同）
#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "common/time_wrapper.h"
#include "domain/food.h"

namespace prosophor::nr::app {

// 一顿饭的账（EndMeal 一次性交出）
struct MealSummary {
    int duration_seconds = 0;                     // "用餐中"→"餐后"的墙钟时长
    std::vector<std::string> dishes;              // 累计菜品（保序去重）
    double total_energy_kcal = 0.0;               // 只累有营养数据的菜（每份口径）
    int dishes_with_nutrition = 0;
    int dishes_without_nutrition = 0;             // 知识库没见过的菜数（诚实表述用）
    int perception_rounds = 0;                    // 实际观察轮数（0 = 这顿没看见）
};

class MealJournal {
public:
    // 进入"用餐中"时调用；已在记账中则忽略（不重置时钟）
    void BeginMeal(prosophor::SteadyClock::TimePoint now);
    // 每轮 agent 上报喂一次（仅在记账中生效）
    void ObserveFoods(const std::vector<prosophor::nr::domain::FoodItem>& foods);
    // 进入"餐后"时调用：产出这顿的账并清空；未 Begin 过返回 nullopt
    std::optional<MealSummary> EndMeal(prosophor::SteadyClock::TimePoint now);

    bool active() const { return active_; }
    // 诊断：当前累计菜品数
    size_t dish_count() const { return summary_.dishes.size(); }

private:
    bool active_ = false;
    prosophor::SteadyClock::TimePoint started_at_{};
    MealSummary summary_;
    std::set<std::string> seen_dishes_;
};

}  // namespace prosophor::nr::app
