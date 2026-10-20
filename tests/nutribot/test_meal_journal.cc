// meal_journal 单测：就餐会话累计（时长/菜品去重/热量只计命中/提醒回顾上限）
#include "meal/meal_journal.h"

#include <chrono>
#include <string>
#include <vector>

#include "domain/events.h"
#include "domain/food.h"
#include "micro_test.hpp"

namespace {

using prosophor::nr::app::MealJournal;
using prosophor::nr::app::MealSummary;
using prosophor::nr::domain::FoodItem;
using prosophor::nr::domain::ReminderEvent;
using prosophor::nr::domain::ScenePackage;

using SteadyClock = prosophor::SteadyClock;
SteadyClock::TimePoint At(int seconds) {
    return SteadyClock::TimePoint{} + std::chrono::seconds(seconds);
}

FoodItem Dish(const std::string& name, double kcal = 0.0, bool has_nutrition = false) {
    FoodItem food(name, name);
    food.nutrition.energy_kcal = kcal;
    food.has_nutrition = has_nutrition;
    return food;
}

ReminderEvent Taboo(const std::string& member, const std::string& food) {
    return ReminderEvent(member, food, "taboo", "warn", member + "：" + food);
}

ScenePackage MakePackage(std::vector<FoodItem> foods = {},
                         std::vector<ReminderEvent> events = {}) {
    ScenePackage pkg;
    pkg.foods = std::move(foods);
    pkg.events = std::move(events);
    return pkg;
}

}  // namespace

TEST(journal, accumulate_duration_dishes_and_energy) {
    MealJournal journal;
    journal.BeginMeal(At(0));
    EXPECT_TRUE(journal.active());

    journal.Observe(MakePackage({Dish("红烧肉", 470.0, true), Dish("清炒时蔬", 90.0, true)}));
    journal.Observe(MakePackage({Dish("红烧肉", 470.0, true)}));   // 重复菜不重复累

    const auto summary = journal.EndMeal(At(125));
    EXPECT_TRUE(summary.has_value());
    EXPECT_FALSE(journal.active());
    EXPECT_EQ(summary->duration_seconds, 125);
    EXPECT_EQ(summary->dishes.size(), 2u);
    EXPECT_EQ(summary->dishes_with_nutrition, 2);
    EXPECT_EQ(summary->dishes_without_nutrition, 0);
    EXPECT_NEAR(summary->total_energy_kcal, 560.0, 1e-9);
    EXPECT_EQ(summary->perception_rounds, 2);
}

TEST(journal, first_occurrence_wins_for_nutrition) {
    MealJournal journal;
    journal.BeginMeal(At(0));
    journal.Observe(MakePackage({Dish("红烧肉")}));                 // 先无营养
    journal.Observe(MakePackage({Dish("红烧肉", 470.0, true)}));    // 后带营养：不补记

    const auto summary = journal.EndMeal(At(10));
    EXPECT_EQ(summary->dishes_with_nutrition, 0);
    EXPECT_EQ(summary->dishes_without_nutrition, 1);
    EXPECT_NEAR(summary->total_energy_kcal, 0.0, 1e-9);
}

TEST(journal, reminders_deduped_and_capped) {
    MealJournal journal;
    journal.BeginMeal(At(0));
    journal.Observe(MakePackage({},
                                {Taboo("小明", "牛奶"), Taboo("小明", "牛奶"),   // 重复
                                 Taboo("小明", "花生"), Taboo("王奶奶", "蛋糕"),
                                 Taboo("王奶奶", "肥肉")}));                    // 第 4 条超限
    const auto summary = journal.EndMeal(At(60));
    EXPECT_EQ(summary->reminders.size(), MealJournal::max_reminders);
    EXPECT_EQ(summary->reminders[0].food, std::string("牛奶"));
}

TEST(journal, end_without_begin_and_double_end) {
    MealJournal journal;
    EXPECT_FALSE(journal.EndMeal(At(0)).has_value());

    journal.BeginMeal(At(0));
    EXPECT_TRUE(journal.EndMeal(At(30)).has_value());
    EXPECT_FALSE(journal.EndMeal(At(40)).has_value());   // 幂等：第二次为空
}

TEST(journal, begin_twice_does_not_reset_clock) {
    MealJournal journal;
    journal.BeginMeal(At(0));
    journal.BeginMeal(At(100));                          // 重复 Begin 被忽略
    const auto summary = journal.EndMeal(At(120));
    EXPECT_EQ(summary->duration_seconds, 120);
}

TEST(journal, observe_ignored_when_inactive) {
    MealJournal journal;
    journal.Observe(MakePackage({Dish("红烧肉", 470.0, true)}));   // 未 Begin
    journal.BeginMeal(At(0));
    const auto summary = journal.EndMeal(At(5));
    EXPECT_EQ(summary->perception_rounds, 0);
    EXPECT_TRUE(summary->dishes.empty());
}

TEST(journal, empty_meal_reports_no_observation) {
    MealJournal journal;
    journal.BeginMeal(At(0));
    const auto summary = journal.EndMeal(At(95));
    EXPECT_EQ(summary->perception_rounds, 0);
    EXPECT_EQ(summary->duration_seconds, 95);
}

TEST(journal, next_meal_starts_clean) {
    MealJournal journal;
    journal.BeginMeal(At(0));
    journal.Observe(MakePackage({Dish("红烧肉", 470.0, true)}, {Taboo("小明", "牛奶")}));
    journal.EndMeal(At(60));

    journal.BeginMeal(At(100));                          // 第二餐：账本干净
    const auto second = journal.EndMeal(At(160));
    EXPECT_TRUE(second->dishes.empty());
    EXPECT_TRUE(second->reminders.empty());
    EXPECT_NEAR(second->total_energy_kcal, 0.0, 1e-9);
    EXPECT_EQ(second->duration_seconds, 60);
}
