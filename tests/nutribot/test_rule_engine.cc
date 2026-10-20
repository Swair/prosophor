// 规则引擎 + 成员禁忌语义单元测试
#include "domain/events.h"
#include "domain/food.h"
#include "domain/member.h"
#include "domain/rule_engine.h"
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

// 带营养的菜（check_nutrition 用例）
FoodItem make_nutri_food(const std::string& name, double kcal, double sodium,
                         double sugar) {
    FoodItem f("f1", name);
    f.nutrition.energy_kcal = kcal;
    f.nutrition.sodium_mg = sodium;
    f.nutrition.sugar_g = sugar;
    f.has_nutrition = true;
    return f;
}

Member make_tagged_member(const std::string& name,
                          std::vector<std::string> health_tags) {
    Member m = make_member(name, {});
    m.profile.health_tags = std::move(health_tags);
    return m;
}

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

TEST(rule_engine, nutrition_skips_foods_without_data) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {make_member("小明", {})};
    // has_nutrition=false（未知）与低钠低糖菜都不产事件
    std::vector<FoodItem> foods = {make_food("宫保鸡丁"),
                                   make_nutri_food("清炒生菜", 60, 320, 2)};
    EXPECT_EQ(engine.check_nutrition(members, foods).size(), 0u);
}

TEST(rule_engine, nutrition_high_sodium_emits_info) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {make_member("小明", {})};
    std::vector<FoodItem> foods = {make_nutri_food("梅菜扣肉", 520, 1100, 8)};

    const auto events = engine.check_nutrition(members, foods);
    EXPECT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, "excess");
    EXPECT_EQ(events[0].level, "info");
    EXPECT_EQ(events[0].member_name, std::string(""));   // 菜品级提示不挂名
    EXPECT_TRUE(events[0].message.find("梅菜扣肉") != std::string::npos);
    EXPECT_TRUE(events[0].message.find("钠") != std::string::npos);
    EXPECT_TRUE(events[0].message.find("1100") != std::string::npos);
}

TEST(rule_engine, nutrition_escalates_to_warn_with_member_tag) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {
        make_member("小明", {}),
        make_tagged_member("王奶奶", {"2型糖尿病", "老年人"})};
    // 高糖菜（奶油蛋糕不是王奶奶的禁忌，此处只测营养规则本身）
    std::vector<FoodItem> foods = {make_nutri_food("草莓挞", 280, 90, 20)};

    const auto events = engine.check_nutrition(members, foods);
    EXPECT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].level, "warn");
    EXPECT_EQ(events[0].member_name, "王奶奶");
    EXPECT_TRUE(events[0].message.find("糖") != std::string::npos);
}

TEST(rule_engine, nutrition_respects_member_dri_override) {
    NutritionRuleEngine engine;
    Member strict = make_member("老张", {});
    strict.profile.dri_overrides["sodium_mg"] = 6000.0;   // 放宽到 2400mg/份
    std::vector<Member> members = {strict};
    std::vector<FoodItem> foods = {make_nutri_food("剁椒鱼头", 320, 1500, 3)};
    EXPECT_EQ(engine.check_nutrition(members, foods).size(), 0u);   // 1500 < 2400

    strict.profile.dri_overrides["sodium_mg"] = 2000.0;   // 收紧回默认口径
    members = {strict};
    EXPECT_EQ(engine.check_nutrition(members, foods).size(), 1u);
}

TEST(rule_engine, nutrition_info_capped_per_round) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {make_member("小明", {})};
    std::vector<FoodItem> foods = {
        make_nutri_food("咸菜A", 100, 900, 1),
        make_nutri_food("咸菜B", 100, 1500, 1),
        make_nutri_food("咸菜C", 100, 1200, 1),
        make_nutri_food("咸菜D", 100, 800, 1),   // 恰好等于阈值：触发（>=）
    };
    const auto events = engine.check_nutrition(members, foods);
    EXPECT_EQ(events.size(), 2u);                // info 上限 2 条
    // 按触发值降序：1500（咸菜B）在前
    EXPECT_TRUE(events[0].message.find("咸菜B") != std::string::npos);
    EXPECT_TRUE(events[1].message.find("咸菜C") != std::string::npos);
}

TEST(rule_engine, nutrition_skipped_when_taboo_already_fired) {
    NutritionRuleEngine engine;
    std::vector<Member> members = {make_member("小明", {"牛奶"})};
    // 牛奶高钠（数据里 120 其实不高，这里构造高钠牛奶验证去重）
    std::vector<FoodItem> foods = {make_nutri_food("牛奶", 160, 900, 12)};

    EXPECT_EQ(engine.check_taboos(members, foods).size(), 1u);      // 禁忌命中
    EXPECT_EQ(engine.check_nutrition(members, foods).size(), 0u);   // 营养不再重复说
}

TEST(member, empty_member_no_taboos) {
    Member m = make_member("路人", {});
    EXPECT_FALSE(m.has_taboo(""));
    EXPECT_FALSE(m.has_taboo("米饭"));
}
