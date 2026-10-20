// meal_schedule 单测：时间串解析、窗口合法性、区间查询边界（含头不含尾）
#include "meal/meal_schedule.h"

#include <optional>
#include <string>

#include "micro_test.hpp"

namespace {

using prosophor::nr::app::MealSchedule;
using prosophor::nr::app::MealWindow;
using prosophor::nr::app::ParseClockMinutes;

}  // namespace

TEST(meal_clock, parses_valid_times) {
    EXPECT_EQ(ParseClockMinutes("07:00").value(), 420);
    EXPECT_EQ(ParseClockMinutes("7:30").value(), 450);
    EXPECT_EQ(ParseClockMinutes("0:00").value(), 0);
    EXPECT_EQ(ParseClockMinutes("23:59").value(), 1439);
    EXPECT_EQ(ParseClockMinutes("7:5").value(), 425);      // 分钟一位数
    EXPECT_EQ(ParseClockMinutes(" 12:30 ").value(), 750);  // 两侧空白
}

TEST(meal_clock, rejects_malformed_times) {
    EXPECT_FALSE(ParseClockMinutes("").has_value());
    EXPECT_FALSE(ParseClockMinutes("abc").has_value());
    EXPECT_FALSE(ParseClockMinutes("7").has_value());       // 缺冒号
    EXPECT_FALSE(ParseClockMinutes("7:").has_value());      // 缺分钟
    EXPECT_FALSE(ParseClockMinutes(":30").has_value());     // 缺小时
    EXPECT_FALSE(ParseClockMinutes("24:00").has_value());   // 小时越界
    EXPECT_FALSE(ParseClockMinutes("7:60").has_value());    // 分钟越界
    EXPECT_FALSE(ParseClockMinutes("1:2:3").has_value());   // 冒号过多
    EXPECT_FALSE(ParseClockMinutes("123:00").has_value());  // 位数过多
    EXPECT_FALSE(ParseClockMinutes("0x7:00").has_value());
}

TEST(meal_window, contains_is_start_inclusive_end_exclusive) {
    const MealWindow window{"午餐", 11 * 60 + 30, 13 * 60 + 30};
    EXPECT_TRUE(window.contains(11 * 60 + 29 + 1));   // 11:30 含
    EXPECT_TRUE(window.contains(12 * 60));
    EXPECT_FALSE(window.contains(11 * 60 + 29));      // 11:29 不含
    EXPECT_FALSE(window.contains(13 * 60 + 30));      // 13:30 不含（边界出）
    EXPECT_FALSE(window.contains(0));
}

TEST(meal_schedule, add_window_validates) {
    MealSchedule schedule;
    EXPECT_TRUE(schedule.AddWindow("早餐", "07:00", "09:00"));
    EXPECT_TRUE(schedule.AddWindow("晚餐", "17:30", "19:30"));
    EXPECT_EQ(schedule.windows().size(), 2u);

    EXPECT_FALSE(schedule.AddWindow("", "07:00", "09:00"));       // 名字空
    EXPECT_FALSE(schedule.AddWindow("加餐", "abc", "09:00"));      // 时间非法
    EXPECT_FALSE(schedule.AddWindow("加餐", "09:00", "07:00"));    // 倒置
    EXPECT_FALSE(schedule.AddWindow("加餐", "09:00", "09:00"));    // 零长
    EXPECT_EQ(schedule.windows().size(), 2u);                      // 非法不入表
}

TEST(meal_schedule, lookup_picks_containing_window) {
    MealSchedule schedule;
    schedule.AddWindow("早餐", "07:00", "09:00");
    schedule.AddWindow("午餐", "11:30", "13:30");

    EXPECT_FALSE(schedule.Lookup(6 * 60).has_value());
    EXPECT_EQ(schedule.Lookup(7 * 60)->name, std::string("早餐"));
    EXPECT_EQ(schedule.Lookup(8 * 60 + 59)->name, std::string("早餐"));
    EXPECT_FALSE(schedule.Lookup(9 * 60).has_value());     // 含头不含尾
    EXPECT_FALSE(schedule.Lookup(11 * 60).has_value());
    EXPECT_EQ(schedule.Lookup(12 * 60)->name, std::string("午餐"));
}

TEST(meal_schedule, lookup_empty_and_overlapping) {
    MealSchedule empty;
    EXPECT_FALSE(empty.Lookup(12 * 60).has_value());
    EXPECT_TRUE(empty.empty());

    MealSchedule overlapping;
    overlapping.AddWindow("先加", "11:00", "14:00");
    overlapping.AddWindow("后加", "12:00", "13:00");
    EXPECT_EQ(overlapping.Lookup(12 * 60)->name, std::string("先加"));  // 取先添加者
}
