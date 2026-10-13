// dish_nutrition：菜品营养知识库（菜名 -> 每份营养）与检测端口装饰器
//
// 数据是**手编演示表**（config/.prosophor/nutribot_dishes.json）：以菜品识别
// 基准的 59 个人工标注菜名为主键起点，按《食物成分表》常用值 + 家常做法折算
// 每份六项；文件级 disclaimer + 条目级 source 双标注，话术一律冠"约"。
//
// 归一匹配：VL 菜名会抖动（实测严格同名基线 26%），精确（规范名/别名）查不到
// 时退到**最长包含匹配**（两侧都要 ≥2 个字符，防"汤"这类单字乱配）；仍不中则
// 保持 has_nutrition=false —— 上下游都诚实处理"没查到"（播报计数"另有 N 道
// 我没见过"，绝不把它当 0 千卡）。
//
// 接入方式：装饰器包住真实检测器（VlFoodDetector），MealMonitor 与规则引擎
// 拿到的就是带营养的菜品 —— Python 1:1 的编排文件零改动。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "food.h"
#include "ports.h"

namespace prosophor::nr::app {

// 知识库一条记录
struct DishNutrition {
    std::string name;                              // 规范名（主键）
    std::vector<std::string> aliases;              // 别名（VL 抖动/同义词）
    prosophor::nr::domain::Nutrition per_serving;  // 每份六项
    std::vector<std::string> tags;                 // 分类标签（高钠/高糖/高脂）
    std::string source;                            // 条目级数据来源（空 = 用文件级）
};

class DishNutritionTable {
public:
    // 从 JSON 文件加载；缺文件/坏 JSON -> nullopt 并填 error；
    // 坏条目（缺名/负值）跳过并计数，合法条目保留
    static std::optional<DishNutritionTable> LoadFromFile(const std::string& path,
                                                          std::string& error);

    // 归一匹配（返回值拷贝：表可自由搬移，调用方不留引用债）
    std::optional<DishNutrition> Match(const std::string& dish_name) const;

    size_t size() const { return dishes_.size(); }
    int skipped_entries() const { return skipped_entries_; }
    // 全部条目（识别闭集候选、覆盖率统计等消费方用）
    const std::vector<DishNutrition>& dishes() const { return dishes_; }

private:
    std::vector<DishNutrition> dishes_;
    int skipped_entries_ = 0;
};

// 端口装饰器：给检测器报出的每道菜挂上营养（未命中保持 has_nutrition=false）
class NutritionEnrichingDetector final : public FoodDetectorPort {
public:
    // inner/table 非拥有；生命周期由装配处保证（inner 必须非空，table 可为空 =
    // 纯透传，等价于没接营养库）
    NutritionEnrichingDetector(FoodDetectorPort* inner, const DishNutritionTable* table);

    std::vector<prosophor::nr::domain::FoodItem> detect_domain(FrameRef frame) override;

    // 上一轮未命中知识库的菜数（诊断/日志）
    int unmatched_last() const { return unmatched_last_; }

private:
    FoodDetectorPort* inner_;
    const DishNutritionTable* table_;
    int unmatched_last_ = 0;
};

}  // namespace prosophor::nr::app
