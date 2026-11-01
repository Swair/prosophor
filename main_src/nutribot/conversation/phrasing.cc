// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "conversation/phrasing.h"

#include <string>

namespace prosophor::nr::app {

namespace {

// 年龄档界：12 以下算儿童（书里的"对儿童用活泼语气"），
// 60 及以上算老人（"对老人用温暖关怀"），中间为成年主体。
constexpr int child_max_age = 12;
constexpr int elder_min_age = 60;

std::string ComposeToneLine(const prosophor::nr::domain::Member& member) {
    std::string line = member.name;

    std::vector<std::string> details;
    if (member.profile.age.has_value()) {
        details.push_back(std::to_string(*member.profile.age) + "岁");
    }
    for (const auto& tag : member.profile.health_tags) details.push_back(tag);

    // 禁忌/过敏原交给 prompt 判断：规则引擎删掉后，这是唯一的信息来源
    std::string avoid;
    for (const auto& item : member.profile.taboo_foods) {
        if (!avoid.empty()) avoid += "、";
        avoid += item;
    }
    for (const auto& item : member.profile.allergies) {
        if (!avoid.empty()) avoid += "、";
        avoid += item;
    }
    if (!avoid.empty()) details.push_back("忌口：" + avoid);

    if (!details.empty()) {
        line += "（";
        for (size_t i = 0; i < details.size(); ++i) {
            if (i > 0) line += "，";
            line += details[i];
        }
        line += "）";
    }
    line += "：" + MemberToneHint(member);
    return line;
}

}  // namespace

std::string MemberToneHint(const prosophor::nr::domain::Member& member) {
    const bool is_child = member.profile.age.has_value() &&
                          *member.profile.age < child_max_age;
    if (is_child) return "活泼鼓励";

    const bool is_elder = member.profile.age.has_value() &&
                          *member.profile.age >= elder_min_age;
    for (const auto& tag : member.profile.health_tags) {
        // 慢病标签优先于年龄：语气重点在"忌口多、要克制"，不是年龄本身
        if (tag.find("糖尿病") != std::string::npos ||
            tag.find("高血压") != std::string::npos ||
            tag.find("高血脂") != std::string::npos) {
            return "克制提醒，语气温和不吓人";
        }
    }
    if (is_elder) return "温暖放慢";
    return "简洁数据";
}

std::string ComposeDinerPromptBlock(
    const std::vector<prosophor::nr::domain::Member>& members) {
    std::string block = "<家庭成员>";
    if (members.empty()) {
        block += "未登记家庭成员档案";
    } else {
        for (size_t i = 0; i < members.size(); ++i) {
            if (i > 0) block += "；";
            block += ComposeToneLine(members[i]);
        }
    }
    block += "</家庭成员>\n";
    return block;
}

}  // namespace prosophor::nr::app
