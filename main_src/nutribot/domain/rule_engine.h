// 领域服务：营养规则引擎（禁忌/营养素比对，纯逻辑、毫秒级）
//
// 移植自 Python domain/rule_engine.py。设计原则：
// 规则比对走规则引擎而非 LLM，LLM 只负责话术生成。
// 无异常 -> 不产生事件（不打扰原则）。
#pragma once

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "domain/events.h"
#include "domain/food.h"
#include "domain/member.h"

namespace prosophor::nr::domain {

class NutritionRuleEngine {
public:
    // 禁忌表/过敏原比对：命中 -> warn 级提醒
    std::vector<ReminderEvent> check_taboos(
        const std::vector<Member>& members,
        const std::vector<FoodItem>& foods) const {
        std::vector<ReminderEvent> events;
        for (const auto& m : members) {
            for (const auto& f : foods) {
                if (m.has_taboo(f.name)) {
                    events.emplace_back(
                        m.name, f.name, "taboo", "warn",
                        m.name + "：" + f.name + "上桌了，你的禁忌食物哦");
                }
            }
        }
        return events;
    }

    // 营养素提示比对（v1 口径：**无份量估算，全部按"每份"计**）。
    //
    // 只做可辩护的"菜品级"提示，不做整桌总量警告 —— 没有份量与进食量数据时，
    // "这顿饭热量超标"是假精确；总热量留给餐后小结陈述，不报警。
    //   高钠：每份钠 >= 0.4 × 日上限（默认 2000mg/日 -> 800mg/份）
    //   高糖：每份糖 >= 0.4 × 日上限（默认 50g/日 -> 20g/份）
    //   默认级 info；在场成员带「高血压/糖尿病」标签 -> 升级 warn 并挂成员名
    // 约束：每轮 info 上限 max_info_per_round 条（按触发值降序，防一桌高钠菜
    // 挤爆播报）；warn 不设限（安全优先）；同一菜已命中禁忌则跳过其营养事件
    // （避免"蛋糕禁忌 + 蛋糕高糖"两条噪音）。
    std::vector<ReminderEvent> check_nutrition(
        const std::vector<Member>& members,
        const std::vector<FoodItem>& foods) const {
        struct Candidate {
            ReminderEvent event;
            double severity = 0.0;   // 触发值（mg 或 g），info 截断排序用
            bool warn = false;
        };

        // 1) 每份阈值：逐成员取"日上限"（dri_overrides 覆盖默认），再取在场最
        //    严格者 —— 覆盖既能收紧（高血压成员调低）也能放宽（个体差异），
        //    所以是"覆盖优先"，不是"与默认取 min"；初值为无穷（无人无事件）
        double sodium_per_serving = std::numeric_limits<double>::max();
        double sugar_per_serving = std::numeric_limits<double>::max();
        std::string hypertension_member;
        std::string diabetes_member;
        for (const auto& m : members) {
            const auto sodium_override = m.profile.dri_overrides.find("sodium_mg");
            const double member_sodium_daily =
                sodium_override != m.profile.dri_overrides.end()
                    ? sodium_override->second
                    : default_daily_sodium_mg;
            sodium_per_serving =
                std::min(sodium_per_serving, member_sodium_daily * per_serving_ratio);

            const auto sugar_override = m.profile.dri_overrides.find("sugar_g");
            const double member_sugar_daily =
                sugar_override != m.profile.dri_overrides.end()
                    ? sugar_override->second
                    : default_daily_sugar_g;
            sugar_per_serving =
                std::min(sugar_per_serving, member_sugar_daily * per_serving_ratio);

            for (const auto& tag : m.profile.health_tags) {
                if (tag.find("高血压") != std::string::npos && hypertension_member.empty()) {
                    hypertension_member = m.name;
                }
                if (tag.find("糖尿病") != std::string::npos && diabetes_member.empty()) {
                    diabetes_member = m.name;
                }
            }
        }

        // 2) 逐菜比对（未接知识库的菜跳过：未知 ≠ 0）
        std::vector<Candidate> candidates;
        for (const auto& f : foods) {
            if (!f.has_nutrition) continue;
            if (AnyMemberTaboo(members, f.name)) continue;

            if (f.nutrition.sodium_mg >= sodium_per_serving) {
                Candidate c;
                c.severity = f.nutrition.sodium_mg;
                c.warn = !hypertension_member.empty();
                const std::string amount =
                    std::to_string(static_cast<int>(f.nutrition.sodium_mg + 0.5));
                c.event = c.warn
                    ? ReminderEvent(hypertension_member, f.name, "excess", "warn",
                                    hypertension_member + "：" + f.name + "钠偏高（每份约 " +
                                        amount + " 毫克），注意控盐")
                    : ReminderEvent("", f.name, "excess", "info",
                                    "「" + f.name + "」钠偏高（每份约 " + amount +
                                        " 毫克），少夹两筷子");
                candidates.push_back(std::move(c));
            }
            if (f.nutrition.sugar_g >= sugar_per_serving) {
                Candidate c;
                c.severity = f.nutrition.sugar_g;
                c.warn = !diabetes_member.empty();
                const std::string amount =
                    std::to_string(static_cast<int>(f.nutrition.sugar_g + 0.5));
                c.event = c.warn
                    ? ReminderEvent(diabetes_member, f.name, "excess", "warn",
                                    diabetes_member + "：" + f.name + "糖偏高（每份约 " +
                                        amount + " 克），注意控糖")
                    : ReminderEvent("", f.name, "excess", "info",
                                    "「" + f.name + "」糖偏高（每份约 " + amount +
                                        " 克），注意把握分量");
                candidates.push_back(std::move(c));
            }
        }

        // 3) warn 全留（按严重度降序）、info 截前 N；warn 在前（安全优先）
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate& a, const Candidate& b) {
                             if (a.warn != b.warn) return a.warn;
                             return a.severity > b.severity;
                         });
        std::vector<ReminderEvent> events;
        int info_count = 0;
        for (auto& c : candidates) {
            if (!c.warn && info_count >= max_info_per_round) continue;
            if (!c.warn) ++info_count;
            events.push_back(std::move(c.event));
        }
        return events;
    }

private:
    // v1 口径常量（《中国居民膳食指南》常见上限口径；dri_overrides 可覆盖）
    static constexpr double default_daily_sodium_mg = 2000.0;   // 钠日上限（≈盐 5g）
    static constexpr double default_daily_sugar_g = 50.0;       // 添加糖日上限
    static constexpr double per_serving_ratio = 0.4;            // 单菜占日上限四成即"偏高"
    static constexpr int max_info_per_round = 2;

    static bool AnyMemberTaboo(const std::vector<Member>& members,
                               const std::string& food_name) {
        for (const auto& m : members) {
            if (m.has_taboo(food_name)) return true;
        }
        return false;
    }
};

}  // namespace prosophor::nr::domain
