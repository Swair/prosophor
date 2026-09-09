// 规则引擎 + 成员禁忌语义单元测试
#include "events.h"
#include "food.h"
#include "member.h"
#include "rule_engine.h"
#include "micro_test.hpp"

using namespace prosophor::nr::domain;

namespace {

Member make_member(const std::string& name,
                   std::vector<std::string> taboos,
                   std::vector<std::string> allergies = {}) {
    Member m;
    m.member_id = "id-" + name;
    m.name = name;
    m.profile.taboo_foods = std::move(taboos);
    m.profile.allergies = std::move(allergies);
    return m;
}

FoodItem make_food(const std::string& name) { return FoodItem("f1", name); }

}  // namespace

TEST(member, taboo_exact_and_substring_match) {
    Member m = make_member("小明", {"牛奶", "花生"});
    EXPECT_TRUE(m.has_taboo("牛奶"));     // 精确
    EXPECT_TRUE(m.has_taboo("花生酱"));   // 子串包含：禁忌「花生」命中「花生酱」
    EXPECT_FALSE(m.has_taboo("花菜"));
    EXPECT_FALSE(m.has_taboo("豆奶"));
}

TEST(member, allergy_merged_with_taboo) {
    Member m = make_member("小红", {"鸡蛋"}, {"虾", "芒果"});
    EXPECT_TRUE(m.has_taboo("鸡蛋"));
    EXPECT_TRUE(m.has_taboo("白灼虾"));   // 过敏原子串命中
    EXPECT_TRUE(m.has_taboo("芒果西米露"));
    EXPECT_FALSE(m.has_taboo("猪肉"));
}

TEST(rule_engine, taboo_hit_emits_warn_event) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {make_member("小明", {"牛奶"})};
    std::vector<FoodItem> foods = {make_food("牛奶"), make_food("清蒸鲈鱼")};

    auto events = engine.check_taboos(members, foods);
    EXPECT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].member_name, "小明");
    EXPECT_EQ(events[0].food, "牛奶");
    EXPECT_EQ(events[0].kind, "taboo");
    EXPECT_EQ(events[0].level, "warn");
    EXPECT_EQ(events[0].message, "小明：牛奶上桌了，你的禁忌食物哦");
}

TEST(rule_engine, no_taboo_no_event_no_interrupt) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {make_member("小明", {"花生"})};
    std::vector<FoodItem> foods = {make_food("清蒸鲈鱼"), make_food("蒜蓉西兰花")};
    auto events = engine.check_taboos(members, foods);
    EXPECT_EQ(events.size(), 0u);   // 不打扰原则
}

TEST(rule_engine, multi_member_multi_food_cartesian) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {make_member("小明", {"牛奶"}),
                                   make_member("小红", {"虾"})};
    std::vector<FoodItem> foods = {make_food("牛奶"), make_food("白灼虾")};
    auto events = engine.check_taboos(members, foods);
    EXPECT_EQ(events.size(), 2u);
}

TEST(rule_engine, nutrition_check_not_implemented_yet) {
    NutritionRuleEngine engine;
    std::vector<Member> members;
    std::vector<FoodItem> foods = {make_food("宫保鸡丁")};
    EXPECT_EQ(engine.check_nutrition(members, foods).size(), 0u);
}

TEST(member, empty_member_no_taboos) {
    Member m = make_member("路人", {});
    EXPECT_FALSE(m.has_taboo(""));
    EXPECT_FALSE(m.has_taboo("米饭"));
}
