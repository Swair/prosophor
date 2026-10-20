// proactive_announcer 单测：播报规划的去重、节流挂起、稳定性防线、饭点问候。
// 时间以固定基准注入（t0 + 秒），行为完全确定，不依赖真实时钟。
#include "conversation/proactive_announcer.h"

#include <chrono>
#include <string>
#include <vector>

#include "domain/events.h"
#include "domain/food.h"
#include "micro_test.hpp"

namespace {

using prosophor::nr::app::AnnouncerTuning;
using prosophor::nr::app::Clock;
using prosophor::nr::app::FoodSetKey;
using prosophor::nr::app::ProactiveAnnouncer;
using prosophor::nr::domain::FoodItem;
using prosophor::nr::domain::ReminderEvent;
using prosophor::nr::domain::ScenePackage;

// 固定时间基准（steady_clock 的起点值无所谓，只做差值）
Clock::TimePoint At(int seconds) {
    return Clock::TimePoint{} + std::chrono::seconds(seconds);
}

ReminderEvent TabooEvent(const std::string& member, const std::string& food) {
    return ReminderEvent(member, food, "taboo", "warn",
                         member + "：" + food + "上桌了，你的禁忌食物哦");
}

ScenePackage MakePackage(std::vector<std::string> foods,
                         std::vector<ReminderEvent> events = {}) {
    ScenePackage pkg;
    for (auto& name : foods) {
        pkg.foods.emplace_back(name, name);
    }
    pkg.events = std::move(events);
    return pkg;
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

TEST(announcer, food_set_key_is_order_insensitive_and_deduped) {
    const std::vector<FoodItem> a = {FoodItem("1", "牛奶"), FoodItem("2", "花生")};
    const std::vector<FoodItem> b = {FoodItem("2", "花生"), FoodItem("1", "牛奶"),
                                     FoodItem("3", "牛奶")};
    EXPECT_EQ(FoodSetKey(a), FoodSetKey(b));
    EXPECT_NE(FoodSetKey(a), FoodSetKey({FoodItem("1", "牛奶")}));
}

TEST(announcer, silent_on_empty_table) {
    ProactiveAnnouncer announcer;
    EXPECT_EQ(announcer.PlanPerception(MakePackage({}), At(0)), std::string(""));
    EXPECT_EQ(announcer.PlanPerception(MakePackage({}), At(3)), std::string(""));
}

TEST(announcer, food_announcement_waits_for_stable_set) {
    ProactiveAnnouncer announcer;
    // 第一轮：不报（集合还没连续两轮一致 —— VL 抖动防线）
    EXPECT_EQ(announcer.PlanPerception(MakePackage({"牛奶", "花生"}), At(0)),
              std::string(""));
    // 第二轮一致：报菜名
    const std::string speech =
        announcer.PlanPerception(MakePackage({"牛奶", "花生"}), At(3));
    EXPECT_TRUE(Contains(speech, "牛奶"));
    EXPECT_TRUE(Contains(speech, "花生"));
    EXPECT_EQ(announcer.stats().food_announcements, 1);

    // 集合未变（乱序喂入）：不重复播
    EXPECT_EQ(announcer.PlanPerception(MakePackage({"花生", "牛奶"}), At(6)),
              std::string(""));
}

TEST(announcer, food_set_change_reannounces_after_gap) {
    ProactiveAnnouncer announcer;
    announcer.PlanPerception(MakePackage({"牛奶"}), At(0));
    announcer.PlanPerception(MakePackage({"牛奶"}), At(3));       // 报了
    announcer.PlanPerception(MakePackage({"牛奶", "清炒时蔬"}), At(6));  // 未稳定
    // 稳定但与上次不同、且过了 10s 节流窗 -> 重报
    const std::string speech =
        announcer.PlanPerception(MakePackage({"牛奶", "清炒时蔬"}), At(20));
    EXPECT_TRUE(Contains(speech, "清炒时蔬"));
    EXPECT_EQ(announcer.stats().food_announcements, 2);
}

TEST(announcer, idle_scene_suppresses_speech_but_keeps_settled_state) {
    // 空桌待机：不做主动开口，但稳定判定照常推进 —— 场景恢复后直接补播，
    // 不用再等两轮稳定（否则真上菜了还要多等一个感知节拍）
    ProactiveAnnouncer announcer;
    const auto pkg = MakePackage({"凉拌木耳"});
    EXPECT_EQ(announcer.PlanPerception(pkg, At(0), /*allow_speech=*/false), std::string(""));
    EXPECT_EQ(announcer.PlanPerception(pkg, At(3), /*allow_speech=*/false), std::string(""));
    EXPECT_EQ(announcer.stats().food_announcements, 0);
    EXPECT_EQ(announcer.stats().suppressed_by_scene, 1);   // 第二次起有话说但被压

    const std::string speech = announcer.PlanPerception(pkg, At(4), /*allow_speech=*/true);
    EXPECT_TRUE(Contains(speech, "凉拌木耳"));
    EXPECT_EQ(announcer.stats().food_announcements, 1);
}

TEST(announcer, idle_scene_holds_reminder_until_allowed) {
    // 访客/空桌静默期识别到的禁忌：挂起不丢，静默结束后补播
    ProactiveAnnouncer announcer;
    const auto pkg = MakePackage({"花生酱拌面"}, {TabooEvent("小明", "花生酱拌面")});
    EXPECT_EQ(announcer.PlanPerception(pkg, At(0), /*allow_speech=*/false), std::string(""));
    EXPECT_EQ(announcer.stats().pending_reminders, 1);
    EXPECT_EQ(announcer.stats().reminders_spoken, 0);

    const std::string speech = announcer.PlanPerception(pkg, At(3), /*allow_speech=*/true);
    EXPECT_TRUE(Contains(speech, "花生酱拌面"));
    EXPECT_EQ(announcer.stats().reminders_spoken, 1);
    EXPECT_EQ(announcer.stats().pending_reminders, 0);
}

TEST(announcer, single_round_flicker_does_not_reannounce) {
    ProactiveAnnouncer announcer;
    announcer.PlanPerception(MakePackage({"牛奶", "花生"}), At(0));
    announcer.PlanPerception(MakePackage({"牛奶", "花生"}), At(3));   // 报了
    // 单轮抖动：只出现一轮少了花生 —— 不构成"稳定变化"
    EXPECT_EQ(announcer.PlanPerception(MakePackage({"牛奶"}), At(6)), std::string(""));
    // 恢复原集合（与已播集合一致）：不重播
    EXPECT_EQ(announcer.PlanPerception(MakePackage({"牛奶", "花生"}), At(9)),
              std::string(""));
    EXPECT_EQ(announcer.stats().food_announcements, 1);
}

TEST(announcer, cleared_table_reannounces_next_meal) {
    ProactiveAnnouncer announcer;
    announcer.PlanPerception(MakePackage({"牛奶"}), At(0));
    announcer.PlanPerception(MakePackage({"牛奶"}), At(3));      // 报了
    announcer.PlanPerception(MakePackage({}), At(6));             // 撤桌（未稳定）
    announcer.PlanPerception(MakePackage({}), At(9));             // 撤桌（稳定，静默记账）
    announcer.PlanPerception(MakePackage({"牛奶"}), At(20));      // 重新上菜（未稳定）
    const std::string speech = announcer.PlanPerception(MakePackage({"牛奶"}), At(23));
    EXPECT_TRUE(Contains(speech, "牛奶"));                         // 新的一餐，重新报
    EXPECT_EQ(announcer.stats().food_announcements, 2);
}

TEST(announcer, reminder_fires_on_first_sighting_and_only_once) {
    // 警告级不等两轮稳定：第一轮就播（安全优先，兑现"放菜到播报"低时延）
    ProactiveAnnouncer announcer;
    const auto pkg = MakePackage({"花生酱拌面"}, {TabooEvent("小明", "花生酱拌面")});
    const std::string speech = announcer.PlanPerception(pkg, At(0));
    EXPECT_TRUE(Contains(speech, "小明"));
    EXPECT_TRUE(Contains(speech, "花生酱拌面"));
    EXPECT_EQ(announcer.stats().reminders_spoken, 1);

    // 同事件不再重复（后续 tick 静默，含稳定后的报菜名也不重复——提醒已带菜名语境）
    EXPECT_EQ(announcer.PlanPerception(pkg, At(3)), std::string(""));
    EXPECT_EQ(announcer.PlanPerception(pkg, At(6)), std::string(""));
}

TEST(announcer, warn_uses_short_gap_after_other_speech) {
    ProactiveAnnouncer announcer;   // 默认 warn_gap_ms=2000
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("晚餐", At(0)), "晚餐"));

    const auto pkg = MakePackage({"花生酱拌面"}, {TabooEvent("小明", "花生酱拌面")});
    EXPECT_EQ(announcer.PlanPerception(pkg, At(1)), std::string(""));   // 1s < 2s 警告窗
    const std::string speech = announcer.PlanPerception(pkg, At(3));    // 3s > 2s -> 播
    EXPECT_TRUE(Contains(speech, "花生酱拌面"));
}

TEST(announcer, reminder_pending_through_gap_then_spoken) {
    // 本用例测"挂起不丢"的长窗行为：把警告窗也设成 10s
    ProactiveAnnouncer announcer(AnnouncerTuning{10000, 10000});
    // 先播一句饭点问候占掉节流窗口
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("午餐", At(0)), "午餐"));

    const auto pkg = MakePackage({"花生酱拌面"}, {TabooEvent("小明", "花生酱拌面")});
    announcer.PlanPerception(pkg, At(1));                        // 未稳定
    EXPECT_EQ(announcer.PlanPerception(pkg, At(4)), std::string(""));  // 稳定但节流中
    EXPECT_TRUE(announcer.stats().suppressed_by_gap >= 1);
    EXPECT_EQ(announcer.stats().pending_reminders, 1);           // 挂起不丢

    const std::string speech = announcer.PlanPerception(pkg, At(11));  // 窗口过后补播
    EXPECT_TRUE(Contains(speech, "花生酱拌面"));
    EXPECT_EQ(announcer.stats().reminders_spoken, 1);
}

TEST(announcer, pending_reminder_dropped_when_food_leaves_table) {
    ProactiveAnnouncer announcer(AnnouncerTuning{10000, 10000});
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("午餐", At(0)), "午餐"));

    const auto pkg = MakePackage({"花生酱拌面"}, {TabooEvent("小明", "花生酱拌面")});
    announcer.PlanPerception(pkg, At(1));
    announcer.PlanPerception(pkg, At(2));    // 稳定，挂起（节流中）
    EXPECT_EQ(announcer.stats().pending_reminders, 1);

    const auto meat = MakePackage({"清炒时蔬"});
    announcer.PlanPerception(meat, At(3));   // 撤下花生酱拌面（未稳定）
    announcer.PlanPerception(meat, At(4));   // 稳定：挂起条目随菜撤桌作废
    EXPECT_EQ(announcer.stats().pending_reminders, 0);

    const std::string speech = announcer.PlanPerception(meat, At(20));   // 只剩报菜名
    EXPECT_FALSE(Contains(speech, "禁忌"));
}

TEST(announcer, meal_entry_greeting_throttled_then_retryable) {
    ProactiveAnnouncer announcer;
    const std::string first = announcer.PlanMealEntry("午餐", At(0));
    EXPECT_TRUE(Contains(first, "午餐"));
    // 紧接着再来一个饭点（演示用）：节流未过 -> 空（调用方下轮重试）
    EXPECT_EQ(announcer.PlanMealEntry("晚餐", At(2)), std::string(""));
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("晚餐", At(10)), "晚餐"));
    EXPECT_EQ(announcer.stats().meal_announcements, 2);
}

TEST(announcer, conversation_speech_resets_gap) {
    ProactiveAnnouncer announcer;
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("午餐", At(0)), "午餐"));
    announcer.NoteConversationSpeech(At(5));             // 用户话轮刚播完
    EXPECT_EQ(announcer.PlanMealEntry("晚餐", At(6)), std::string(""));   // 基准被刷新
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("晚餐", At(15)), "晚餐"));
}

TEST(announcer, empty_event_message_is_ignored) {
    ProactiveAnnouncer announcer;
    ScenePackage pkg;
    pkg.events.emplace_back("小明", "牛奶", "taboo", "warn", "");   // 异常产物
    EXPECT_EQ(announcer.PlanPerception(pkg, At(0)), std::string(""));
    EXPECT_EQ(announcer.PlanPerception(pkg, At(3)), std::string(""));
    EXPECT_EQ(announcer.stats().reminders_spoken, 0);
    EXPECT_EQ(announcer.stats().pending_reminders, 0);
}

// ---- 报菜名带热量（Phase 1"说出菜名和大致热量"）----

namespace {

prosophor::nr::domain::FoodItem NutriDish(const std::string& name, double kcal) {
    prosophor::nr::domain::FoodItem food(name, name);
    food.nutrition.energy_kcal = kcal;
    food.has_nutrition = true;
    return food;
}

}  // namespace

TEST(announcer, food_announcement_includes_calories_when_all_matched) {
    ProactiveAnnouncer announcer;
    const auto pkg = MakePackage({});
    auto with_foods = pkg;
    with_foods.foods = {NutriDish("红烧肉", 470.0), NutriDish("清炒时蔬", 90.0)};

    announcer.PlanPerception(with_foods, At(0));           // 未稳定
    const std::string speech = announcer.PlanPerception(with_foods, At(3));
    EXPECT_TRUE(Contains(speech, "红烧肉"));
    EXPECT_TRUE(Contains(speech, "合计约 560 千卡"));
    EXPECT_FALSE(Contains(speech, "没见过"));
}

TEST(announcer, food_announcement_counts_unmatched_honestly) {
    ProactiveAnnouncer announcer;
    ScenePackage pkg;
    pkg.foods = {NutriDish("红烧肉", 470.0), MakePackage({"神秘菜品"}).foods[0]};
    announcer.PlanPerception(pkg, At(0));
    const std::string speech = announcer.PlanPerception(pkg, At(3));
    EXPECT_TRUE(Contains(speech, "合计约 470 千卡"));
    EXPECT_TRUE(Contains(speech, "另有 1 道我没见过"));
}

TEST(announcer, food_announcement_falls_back_to_names_only) {
    ProactiveAnnouncer announcer;
    const auto pkg = MakePackage({"清蒸鲈鱼", "白灼虾"});   // 无营养数据（旧行为回归）
    announcer.PlanPerception(pkg, At(0));
    const std::string speech = announcer.PlanPerception(pkg, At(3));
    EXPECT_EQ(speech, std::string("桌上现在有：清蒸鲈鱼、白灼虾。"));
}

TEST(announcer, food_announcement_limits_dish_names) {
    ProactiveAnnouncer announcer;
    ScenePackage pkg;
    for (int i = 1; i <= 8; ++i) {
        pkg.foods.push_back(NutriDish("菜" + std::to_string(i), 100.0));
    }
    announcer.PlanPerception(pkg, At(0));
    const std::string speech = announcer.PlanPerception(pkg, At(3));
    EXPECT_TRUE(Contains(speech, "…等 8 道菜"));
    EXPECT_TRUE(Contains(speech, "合计约 800 千卡"));
    EXPECT_FALSE(Contains(speech, "菜7"));                 // 截到 6 道
}

// ---- 餐后小结 ----

namespace {

prosophor::nr::app::MealSummary MakeSummary() {
    prosophor::nr::app::MealSummary summary;
    summary.duration_seconds = 1500;             // 25 分钟
    summary.dishes = {"红烧肉", "清炒时蔬", "紫菜蛋花汤"};
    summary.total_energy_kcal = 612.0;
    summary.dishes_with_nutrition = 3;
    summary.perception_rounds = 12;
    return summary;
}

}  // namespace

TEST(announcer, meal_summary_full_text) {
    using prosophor::nr::app::MealSummary;
    ProactiveAnnouncer announcer;
    MealSummary summary = MakeSummary();
    summary.reminders = {TabooEvent("小明", "花生酱拌面")};

    const std::string speech = announcer.PlanMealSummary(summary, At(0));
    EXPECT_TRUE(Contains(speech, "这顿饭用了 25 分钟"));
    EXPECT_TRUE(Contains(speech, "一共 3 道菜"));
    EXPECT_TRUE(Contains(speech, "合计约 610 千卡"));      // 612 -> 取整到 610
    EXPECT_TRUE(Contains(speech, "刚才提醒过："));
    EXPECT_TRUE(Contains(speech, "花生酱拌面"));
    EXPECT_EQ(announcer.stats().meal_summaries, 1);
}

TEST(announcer, meal_summary_no_observation_is_honest) {
    using prosophor::nr::app::MealSummary;
    ProactiveAnnouncer announcer;
    MealSummary summary;
    summary.duration_seconds = 90;
    const std::string speech = announcer.PlanMealSummary(summary, At(0));
    EXPECT_TRUE(Contains(speech, "没看清桌上有什么菜"));
    EXPECT_FALSE(Contains(speech, "千卡"));
}

TEST(announcer, meal_summary_throttled_then_retryable) {
    using prosophor::nr::app::MealSummary;
    ProactiveAnnouncer announcer;
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("晚餐", At(0)), "晚餐"));

    const MealSummary summary = MakeSummary();
    EXPECT_EQ(announcer.PlanMealSummary(summary, At(3)), std::string(""));   // 节流中
    EXPECT_TRUE(Contains(announcer.PlanMealSummary(summary, At(11)), "这顿饭"));
    EXPECT_EQ(announcer.stats().meal_summaries, 1);
}

TEST(announcer, meal_entry_with_salutation_prepends_name) {
    ProactiveAnnouncer announcer;
    const std::string speech = announcer.PlanMealEntry("晚餐", At(0), "小明");
    EXPECT_TRUE(Contains(speech, "小明，到晚餐时间啦"));
}

TEST(announcer, meal_entry_without_salutation_keeps_generic) {
    ProactiveAnnouncer announcer;
    EXPECT_TRUE(Contains(announcer.PlanMealEntry("晚餐", At(0)), "到晚餐时间啦"));
}

TEST(announcer, meal_summary_with_salutation_prepends_name) {
    using prosophor::nr::app::MealSummary;
    ProactiveAnnouncer announcer;
    const std::string speech = announcer.PlanMealSummary(MakeSummary(), At(0), "小明");
    EXPECT_TRUE(Contains(speech, "小明，这顿饭"));
}
