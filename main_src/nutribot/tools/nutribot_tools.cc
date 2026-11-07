// NutriBot 的领域工具注册：成员档案（机器人动作工具见同目录）。
// 感知不走工具调用，而是独立 session（无工具、保留文字上下文）的 JSON 提取
// （RunPerceptionTurn）。
#include "entry/resident_session.h"

#include <set>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "prosophor_core/public/agent_engine.h"

namespace prosophor::nr::nutribot {

ResidentSession::TableReport ResidentSession::BuildTableReport(
    const nlohmann::json& args) {
    TableReport report;
    report.has_food = args.value("has_food", false);
    report.scene = args.value("scene", "");
    report.description = args.value("reply", "");
    // 场景是闭集；模型偶尔自由发挥（如"家庭聚餐"），非法值直接忽略。
    static const std::set<std::string> valid_scenes = {
        "空桌待机", "备餐中", "用餐中", "餐后"};
    if (valid_scenes.count(report.scene) == 0) report.scene.clear();
    std::set<std::string> seen;
    if (args.contains("dishes") && args["dishes"].is_array()) {
        for (const auto& item : args["dishes"]) {
            if (!item.is_string()) continue;
            std::string raw = item.get<std::string>();
            if (raw.empty() || !seen.insert(raw).second) continue;
            prosophor::nr::domain::FoodItem food(raw, raw);
            if (nutrition_table_.has_value()) {
                if (auto match = nutrition_table_->Match(raw); match.has_value()) {
                    food.name = match->name;
                    food.nutrition = match->per_serving;
                    food.has_nutrition = true;
                }
            }
            report.foods.push_back(std::move(food));
            if (report.foods.size() >= 5) break;
        }
    }
    if (!report.has_food) report.foods.clear();
    report.valid = true;
    return report;
}

void ResidentSession::RegisterMemberProfileTool() {
    using nlohmann::json;

    // 成员档案：agent 自己问字段、自己读写
    json profile_schema = {
        {"type", "object"},
        {"properties",
         {{"action", {{"type", "string"}, {"enum", {"get", "save"}}}},
          {"name", {{"type", "string"}, {"description", "成员姓名"}}},
          {"age", {{"type", "integer"}}},
          {"gender", {{"type", "string"}}},
          {"taboo_foods",
           {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"allergies",
           {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"eating_habits",
           {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"preferences",
           {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"health_tags",
           {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
        {"required", {"action"}}};
    engine_->RegisterTool(
        "member_profile",
        "读取或保存家庭成员健康档案。action=get 查询（name 为空返回全部）；"
        "action=save 新建或更新。建档时先 get 查重，再按用户回答 save。",
        std::move(profile_schema),
        [this](const json& args) -> std::string {
            const std::string action = args.value("action", "get");
            if (action == "get") {
                const std::string name = args.value("name", "");
                json out = json::array();
                for (const auto& m : members_->list_members()) {
                    if (!name.empty() && m.name != name) continue;
                    json item;
                    item["name"] = m.name;
                    item["member_id"] = m.member_id;
                    if (m.profile.age.has_value()) item["age"] = *m.profile.age;
                    item["gender"] = m.profile.gender;
                    item["taboo_foods"] = m.profile.taboo_foods;
                    item["allergies"] = m.profile.allergies;
                    item["eating_habits"] = m.profile.eating_habits;
                    item["preferences"] = m.profile.preferences;
                    item["health_tags"] = m.profile.health_tags;
                    out.push_back(std::move(item));
                }
                return out.dump();
            }
            if (action != "save") return "error: unknown action";
            const std::string name = args.value("name", "");
            if (name.empty()) return "error: name required";
            auto existing = members_->get_member(name);
            prosophor::nr::domain::Member member =
                existing.value_or(prosophor::nr::domain::Member{});
            member.name = name;
            if (member.member_id.empty()) member.member_id = "member-" + name;
            if (args.contains("age") && args["age"].is_number_integer()) {
                member.profile.age = args["age"].get<int>();
            }
            if (args.contains("gender") && args["gender"].is_string()) {
                member.profile.gender = args["gender"].get<std::string>();
            }
            // 未提供的字段保持原值：agent 可能只保存本次问到的新信息
            const auto read_list = [&args](const char* key,
                                           std::vector<std::string>& out) {
                if (!args.contains(key) || !args[key].is_array()) return;
                out.clear();
                for (const auto& item : args[key]) {
                    if (item.is_string()) out.push_back(item.get<std::string>());
                }
            };
            read_list("taboo_foods", member.profile.taboo_foods);
            read_list("allergies", member.profile.allergies);
            read_list("eating_habits", member.profile.eating_habits);
            read_list("preferences", member.profile.preferences);
            read_list("health_tags", member.profile.health_tags);
            members_->add_member(member);
            return "ok";
        });
}

}  // namespace prosophor::nr::nutribot
