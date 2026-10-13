// dish_nutrition 单测：加载/坏条目跳过/归一匹配/装饰器（夹具在 assets/）
#include "dish_nutrition.h"

#include <string>
#include <vector>

#include "common/file_utils.h"
#include "food.h"
#include "micro_test.hpp"
#include "ports.h"

namespace {

using prosophor::nr::app::DishNutritionTable;
using prosophor::nr::app::NutritionEnrichingDetector;
using prosophor::nr::domain::FoodItem;

// 夹具路径从 __FILE__ 推导：测试二进制的工作目录是构建目录，不能依赖相对路径
std::string FixturePath(const std::string& name) {
    return prosophor::JoinPath(prosophor::ParentDir(__FILE__), "assets", name);
}

DishNutritionTable LoadFixtureOrThrow() {
    std::string error;
    auto table = DishNutritionTable::LoadFromFile(FixturePath("dish_nutrition_sample.json"), error);
    if (!table.has_value()) {
        ::micro_test::report_failure(__FILE__, __LINE__, "夹具加载失败: " + error);
    }
    return *table;
}

// 内层 Fake：固定产出若干菜名（装饰器用例）
class FakeInner : public prosophor::nr::app::FoodDetectorPort {
public:
    std::vector<FoodItem> foods;
    std::vector<FoodItem> detect_domain(prosophor::nr::app::FrameRef) override {
        return foods;
    }
};

}  // namespace

TEST(dish_nutrition, load_fixture_counts_valid_and_skipped) {
    const DishNutritionTable table = LoadFixtureOrThrow();
    EXPECT_EQ(table.size(), 5u);           // 红烧肉/麻婆豆腐/麻辣豆腐/紫菜蛋花汤/汤
    EXPECT_EQ(table.skipped_entries(), 4); // 空名/负值/缺营养段/非对象
}

TEST(dish_nutrition, load_missing_file_and_bad_json) {
    std::string error;
    EXPECT_FALSE(
        DishNutritionTable::LoadFromFile(FixturePath("不存在.json"), error).has_value());
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(DishNutritionTable::LoadFromFile(
                     prosophor::JoinPath(prosophor::ParentDir(__FILE__), "assets",
                                         "dishes", "labels.tsv"),
                     error)
                     .has_value());
    EXPECT_FALSE(error.empty());   // TSV 不是 JSON
}

TEST(dish_nutrition, exact_and_alias_match) {
    const DishNutritionTable table = LoadFixtureOrThrow();

    const auto dish = table.Match("红烧肉");
    EXPECT_TRUE(dish.has_value());
    EXPECT_NEAR(dish->per_serving.energy_kcal, 470.0, 1e-9);
    EXPECT_NEAR(dish->per_serving.sodium_mg, 900.0, 1e-9);

    const auto alias = table.Match("红烧五花肉");
    EXPECT_TRUE(alias.has_value());
    EXPECT_EQ(alias->name, std::string("红烧肉"));

    EXPECT_FALSE(table.Match("没见过的菜").has_value());
}

TEST(dish_nutrition, normalization_strips_whitespace) {
    const DishNutritionTable table = LoadFixtureOrThrow();
    EXPECT_TRUE(table.Match("  紫菜蛋花汤  ").has_value());
    EXPECT_TRUE(table.Match("红烧 肉").has_value());       // 内部空白也剔除
    EXPECT_FALSE(table.Match("   ").has_value());
}

TEST(dish_nutrition, containment_match_prefers_longest) {
    DishNutritionTable table = LoadFixtureOrThrow();
    // VL 常带做法后缀："红烧肉盖饭" 应命中 "红烧肉"
    const auto with_suffix = table.Match("红烧肉盖饭");
    EXPECT_TRUE(with_suffix.has_value());
    EXPECT_EQ(with_suffix->name, std::string("红烧肉"));

    // 两侧都在：等长取先出现者（夹具里麻婆豆腐在前）
    const auto tofu = table.Match("豆腐");
    EXPECT_TRUE(tofu.has_value());
    EXPECT_EQ(tofu->name, std::string("麻婆豆腐"));
}

TEST(dish_nutrition, single_char_does_not_substring_match) {
    const DishNutritionTable table = LoadFixtureOrThrow();
    // "肉" 是单字：不做包含匹配（防"汤/肉"这类单字乱配），且无精确条目
    EXPECT_FALSE(table.Match("肉").has_value());
    // 但单字精确条目仍可命中（夹具里就有"汤"）
    const auto soup = table.Match("汤");
    EXPECT_TRUE(soup.has_value());
    EXPECT_EQ(soup->name, std::string("汤"));
}

TEST(dish_nutrition, decorator_enriches_and_counts_unmatched) {
    const DishNutritionTable table = LoadFixtureOrThrow();
    FakeInner inner;
    inner.foods = {FoodItem("1", "红烧肉"), FoodItem("2", "神秘菜品")};
    NutritionEnrichingDetector detector(&inner, &table);

    const auto foods = detector.detect_domain(nullptr);
    EXPECT_EQ(foods.size(), 2u);                    // 顺序保持
    EXPECT_TRUE(foods[0].has_nutrition);
    EXPECT_NEAR(foods[0].nutrition.energy_kcal, 470.0, 1e-9);
    EXPECT_FALSE(foods[1].has_nutrition);           // 未命中：未知 ≠ 0 千卡
    EXPECT_EQ(detector.unmatched_last(), 1);
}

TEST(dish_nutrition, decorator_canonicalizes_matched_names) {
    const DishNutritionTable table = LoadFixtureOrThrow();
    FakeInner inner;
    inner.foods = {FoodItem("1", "红烧肉盖饭"), FoodItem("2", "红烧五花肉")};   // 包含/别名变体
    NutritionEnrichingDetector detector(&inner, &table);

    const auto foods = detector.detect_domain(nullptr);
    EXPECT_EQ(foods[0].name, std::string("红烧肉"));   // 归一为规范名
    EXPECT_EQ(foods[1].name, std::string("红烧肉"));
    EXPECT_TRUE(foods[0].has_nutrition);
}

TEST(dish_nutrition, decorator_passthrough_without_table) {
    FakeInner inner;
    inner.foods = {FoodItem("1", "红烧肉")};
    NutritionEnrichingDetector detector(&inner, nullptr);
    const auto foods = detector.detect_domain(nullptr);
    EXPECT_EQ(foods.size(), 1u);
    EXPECT_FALSE(foods[0].has_nutrition);
    EXPECT_EQ(detector.unmatched_last(), 0);        // 没接知识库：不算"没见过"
}

TEST(dish_nutrition, decorator_empty_inner) {
    const DishNutritionTable table = LoadFixtureOrThrow();
    FakeInner inner;
    NutritionEnrichingDetector detector(&inner, &table);
    EXPECT_TRUE(detector.detect_domain(nullptr).empty());
    EXPECT_EQ(detector.unmatched_last(), 0);
}
