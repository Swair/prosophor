// 领域事件：提醒事件与结构化场景包
//
// 移植自 Python domain/events.py。JSON 序列化（to_dict）在接入
// 官方序列化方案后补充，本层只保留纯数据结构。
#pragma once

#include <array>
#include <string>
#include <vector>

#include "food.h"

namespace prosophor::nr::domain {

// 提醒事件：规则引擎产出，进入对话队列（限频后 TTS 播报）
struct ReminderEvent {
    std::string member_name;
    std::string food;
    // kind: taboo 禁忌 / excess 超标 / deficiency 缺乏
    std::string kind = "taboo";
    // level: warn / info
    std::string level = "warn";
    std::string message;

    ReminderEvent() = default;
    ReminderEvent(std::string member, std::string food_name,
                  std::string event_kind, std::string event_level,
                  std::string text)
        : member_name(std::move(member)), food(std::move(food_name)),
          kind(std::move(event_kind)), level(std::move(event_level)),
          message(std::move(text)) {}
};

// 场景包成员条目（对应 Python FaceObservation.to_dict 的字段集）
struct MemberBrief {
    std::string name;
    double similarity = 0.0;                 // 相似度（构造处保留 3 位语义）
    std::vector<std::string> tags;           // health_tags
    std::vector<std::string> taboo;          // taboo_foods
    std::array<int, 4> box{};                // (x, y, w, h)
};

// 结构化场景包（1Hz，Agent 层消费接口；对应 Python ScenePackage）
struct ScenePackage {
    double timestamp = 0.0;
    std::string scene_state;                 // 状态中文标签（"unknown" 语义同 Python）
    std::vector<MemberBrief> members;        // 在场成员条目
    std::vector<FoodItem> foods;             // 桌上菜品
    std::vector<ReminderEvent> events;       // 提醒事件
};

}  // namespace prosophor::nr::domain
