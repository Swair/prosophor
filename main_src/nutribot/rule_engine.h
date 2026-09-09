// 领域服务：营养规则引擎（禁忌/营养素比对，纯逻辑、毫秒级）
//
// 移植自 Python domain/rule_engine.py。设计原则：
// 规则比对走规则引擎而非 LLM，LLM 只负责话术生成。
// 无异常 -> 不产生事件（不打扰原则）。
#pragma once

#include <vector>

#include "events.h"
#include "food.h"
#include "member.h"

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

    // 营养素超标/缺乏比对（Phase 3 营养库与份量估算落地后启用）
    std::vector<ReminderEvent> check_nutrition(
        const std::vector<Member>& /*members*/,
        const std::vector<FoodItem>& /*foods*/) const {
        // TODO(Phase 3): 营养库 + DRI 标准表落地后实现（foods 需带份量估算）
        return {};
    }
};

}  // namespace prosophor::nr::domain
