// dish_nutrition：菜品营养知识库（菜名 -> 每份营养）与检测端口装饰器
//
// 数据是**手编演示表**（config/.prosophor/nutribot_dishes.json）：以菜品识别
// 基准的 59 个人工标注菜名为主键起点，按《食物成分表》常用值 + 家常做法折算
// 每份六项；文件级 disclaimer 标注来源，话术一律冠"约"。
//
// 归一匹配：VL 菜名会抖动（实测严格同名基线 26%），精确（规范名/别名）查不到
// 时退到**最长包含匹配**（两侧都要 ≥2 个字符，防"汤"这类单字乱配）；仍不中则
// 保持 has_nutrition=false —— 上下游都诚实处理"没查到"（播报计数"另有 N 道
// 我没见过"，绝不把它当 0 千卡）。
//
// 接入方式：ResidentSession 在周期感知 JSON 解析时按菜名查表，
// 给 agent 上报的菜挂上每份营养；prompt/餐账拿到的就是带营养的菜品。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/food.h"

namespace prosophor::nr::app {

// 知识库一条记录
struct DishNutrition {
    std::string name;                              // 规范名（主键）
    std::vector<std::string> aliases;              // 别名（VL 抖动/同义词）
    prosophor::nr::domain::Nutrition per_serving;  // 每份六项
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

}  // namespace prosophor::nr::app
