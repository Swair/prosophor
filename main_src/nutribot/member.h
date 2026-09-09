// 领域实体：成员（Member）与健康档案值对象（HealthProfile）
//
// 移植自 Python domain/member.py，纯数据结构、无 I/O、无第三方依赖。
// 注：dri_overrides 的 JSON 序列化在接入官方序列化方案前不在此层实现。
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace prosophor::nr::domain {

// 成员健康档案（值对象，不可变语义）
struct HealthProfile {
    std::optional<int> age;                       // 年龄（岁）
    std::string gender;                           // 性别: "男" / "女"（空=未填）
    std::optional<double> height_cm;              // 身高（cm）
    std::optional<double> weight_kg;              // 体重（kg）
    std::vector<std::string> health_tags;         // 如 ["儿童","2型糖尿病","孕中期"]
    std::vector<std::string> taboo_foods;         // 禁忌食物: 如 ["牛奶","花生"]
    std::vector<std::string> allergies;           // 过敏原: 如 ["虾","芒果"]
    std::unordered_map<std::string, double> dri_overrides;  // 如 {"energy_kcal": 1800}
    std::vector<std::string> eating_habits;       // 如 ["少盐","素食"]
    std::vector<std::string> preferences;         // 如 ["清淡","甜食"]
};

// 成员实体：身份 + 健康档案 + 人脸特征（512-d 多张）
struct Member {
    std::string member_id;                        // 唯一标识（内部主键）
    std::string name;                             // 姓名（显示/识别输出）
    HealthProfile profile;                        // 健康档案（值对象）
    std::vector<std::vector<float>> face_embeddings;  // 512-d，可多张
    std::string registered_at;                    // 注册时间 ISO 字符串

    // 判断该成员是否禁忌某食物（禁忌 + 过敏原合并比对）。
    //
    // 中文食物名用包含关系匹配：禁忌「花生」可命中菜品「花生酱」；
    // 精确匹配同样成立（禁忌「牛奶」命中「牛奶」）。
    bool has_taboo(const std::string& food_name) const {
        const auto& terms = profile.taboo_foods;
        const auto& allergies = profile.allergies;
        for (const auto& t : terms) {
            if (food_name == t || (!t.empty() && food_name.find(t) != std::string::npos))
                return true;
        }
        for (const auto& a : allergies) {
            if (food_name == a || (!a.empty() && food_name.find(a) != std::string::npos))
                return true;
        }
        return false;
    }
};

}  // namespace prosophor::nr::domain
