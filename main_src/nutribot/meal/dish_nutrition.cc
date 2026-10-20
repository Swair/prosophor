// DishNutritionTable / NutritionEnrichingDetector 实现（详见头注释）
#include "meal/dish_nutrition.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "common/file_utils.h"
#include "common/log_wrapper.h"

namespace prosophor::nr::app {

using nlohmann::json;

namespace {

// UTF-8 码点数（用于"两侧长度≥2 个字符"的包含匹配门槛；中文一字 3 字节）
size_t CodepointCount(const std::string& text) {
    size_t count = 0;
    for (const unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;   // 非续接字节 = 一个新码点
    }
    return count;
}

// 归一：剔除所有空白（菜名里不存在有意义的空格；" 红烧  肉 " -> "红烧肉"）
std::string NormalizeName(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        out += c;
    }
    return out;
}

// 六字段读取：任一字段为负数 -> 视为坏条目（返回 false）
bool ReadNutrition(const json& node, prosophor::nr::domain::Nutrition& out) {
    if (!node.is_object()) return false;
    const auto read = [&node](const char* key) -> std::optional<double> {
        const auto it = node.find(key);
        if (it == node.end() || !it->is_number()) return std::nullopt;
        return it->get<double>();
    };
    const struct {
        const char* key;
        double* field;
    } fields[] = {
        {"energy_kcal", &out.energy_kcal}, {"protein_g", &out.protein_g},
        {"fat_g", &out.fat_g},             {"carb_g", &out.carb_g},
        {"sodium_mg", &out.sodium_mg},     {"sugar_g", &out.sugar_g},
    };
    for (const auto& field : fields) {
        const auto value = read(field.key);
        if (value.has_value()) {
            if (*value < 0.0) return false;   // 负营养值 = 数据错
            *field.field = *value;
        }
    }
    return true;
}

}  // namespace

std::optional<DishNutritionTable> DishNutritionTable::LoadFromFile(
    const std::string& path, std::string& error) {
    const auto document = prosophor::ReadJson(path);
    if (!document.has_value()) {
        error = "营养库文件不存在或不可读: " + path;
        return std::nullopt;
    }
    if (!document->contains("dishes") || !(*document)["dishes"].is_array()) {
        error = "营养库缺少 dishes 数组: " + path;
        return std::nullopt;
    }

    DishNutritionTable table;
    for (const auto& item : (*document)["dishes"]) {
        DishNutrition dish;
        if (!item.is_object() || !item.contains("name") ||
            !item["name"].is_string()) {
            ++table.skipped_entries_;
            continue;
        }
        dish.name = NormalizeName(item["name"].get<std::string>());
        if (dish.name.empty()) {
            ++table.skipped_entries_;
            continue;
        }
        if (item.contains("aliases") && item["aliases"].is_array()) {
            for (const auto& alias : item["aliases"]) {
                if (alias.is_string()) dish.aliases.push_back(alias.get<std::string>());
            }
        }
        const auto nutrition = item.find("per_serving");
        if (nutrition == item.end() || !ReadNutrition(*nutrition, dish.per_serving)) {
            ++table.skipped_entries_;
            continue;
        }
        if (item.contains("tags") && item["tags"].is_array()) {
            for (const auto& tag : item["tags"]) {
                if (tag.is_string()) dish.tags.push_back(tag.get<std::string>());
            }
        }
        dish.source = item.value("source", std::string(""));
        table.dishes_.push_back(std::move(dish));
    }
    return table;
}

std::optional<DishNutrition> DishNutritionTable::Match(
    const std::string& dish_name) const {
    const std::string name = NormalizeName(dish_name);
    if (name.empty()) return std::nullopt;

    // 1) 精确：规范名 / 别名
    for (const auto& dish : dishes_) {
        if (dish.name == name) return dish;
        for (const auto& alias : dish.aliases) {
            if (alias == name) return dish;
        }
    }

    // 2) 最长包含匹配：两侧都要 ≥2 个字符（"汤"这类单字不做包含，太容易乱配）
    if (CodepointCount(name) < 2) return std::nullopt;
    const DishNutrition* best = nullptr;
    for (const auto& dish : dishes_) {
        if (CodepointCount(dish.name) < 2) continue;
        const bool related = name.find(dish.name) != std::string::npos ||
                             dish.name.find(name) != std::string::npos;
        if (!related) continue;
        if (best == nullptr || dish.name.size() > best->name.size()) {
            best = &dish;   // 等长取先出现者（稳定、可测）
        }
    }
    if (best == nullptr) return std::nullopt;
    return *best;
}

NutritionEnrichingDetector::NutritionEnrichingDetector(FoodDetectorPort* inner,
                                                       const DishNutritionTable* table)
    : inner_(inner), table_(table) {}

std::vector<prosophor::nr::domain::FoodItem> NutritionEnrichingDetector::detect_domain(
    FrameRef frame) {
    std::vector<prosophor::nr::domain::FoodItem> foods = inner_->detect_domain(frame);
    unmatched_last_ = 0;
    if (table_ == nullptr) return foods;   // 没接知识库：纯透传

    for (auto& food : foods) {
        const auto match = table_->Match(food.name);
        if (!match.has_value()) {
            ++unmatched_last_;
            LOG_DEBUG("nutribot: 营养库未见过的菜「{}」", food.name.c_str());
            continue;   // has_nutrition 保持 false：未知 ≠ 0 千卡
        }
        // 命中即归一为规范名：VL 的抖动变体（"炒生菜"/别名"煎蛋"）归到库内主键，
        // 下游的营养命中、禁忌比对、餐账去重都以同一口径工作
        food.name = match->name;
        food.nutrition = match->per_serving;
        food.has_nutrition = true;
    }
    return foods;
}

}  // namespace prosophor::nr::app
