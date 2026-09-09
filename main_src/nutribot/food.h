// 领域值对象：菜品（FoodItem）与营养素（Nutrition）
//
// 移植自 Python domain/food.py。Phase 3 食物识别 + 中国食物成分表使用。
#pragma once

#include <string>

namespace prosophor::nr::domain {

// 营养素组成（每份，单位见字段名）
struct Nutrition {
    double energy_kcal = 0.0;   // 千卡
    double protein_g = 0.0;     // 蛋白质 g
    double fat_g = 0.0;         // 脂肪 g
    double carb_g = 0.0;        // 碳水 g
    double sodium_mg = 0.0;     // 钠 mg
    double sugar_g = 0.0;       // 糖 g
};

// 桌上菜品（实体）
struct FoodItem {
    std::string food_id;
    std::string name;
    double confidence = 1.0;
    Nutrition nutrition;

    FoodItem() = default;
    FoodItem(std::string id, std::string nm, double conf = 1.0)
        : food_id(std::move(id)), name(std::move(nm)), confidence(conf) {}
};

}  // namespace prosophor::nr::domain
