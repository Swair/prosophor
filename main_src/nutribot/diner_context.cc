// diner_context 实现 —— 在场成员路由（纯逻辑，零 IO）
#include "diner_context.h"

#include <algorithm>

namespace prosophor::nr::app {

namespace {

// 年龄档界：12 以下算儿童（书里的"对儿童用活泼语气"），
// 60 及以上算老人（"对老人用温暖关怀"），中间为成年主体。
constexpr int child_max_age = 12;
constexpr int elder_min_age = 60;

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

std::string DirectAddress(const std::vector<prosophor::nr::domain::Member>& members) {
    for (const auto& member : members) {
        if (!member.name.empty() && member.profile.age.has_value() &&
            *member.profile.age < child_max_age) {
            return member.name;
        }
    }
    return {};
}

std::string ComposeToneLine(const prosophor::nr::domain::Member& member) {
    std::string line = member.name;
    if (member.profile.age.has_value()) {
        line += "（" + std::to_string(*member.profile.age) + "岁";
        if (!member.profile.health_tags.empty()) {
            line += "，";
            for (size_t i = 0; i < member.profile.health_tags.size(); ++i) {
                if (i > 0) line += "、";
                line += member.profile.health_tags[i];
            }
        }
        line += "）";
    } else if (!member.profile.health_tags.empty()) {
        line += "（";
        for (size_t i = 0; i < member.profile.health_tags.size(); ++i) {
            if (i > 0) line += "、";
            line += member.profile.health_tags[i];
        }
        line += "）";
    }
    line += "：" + MemberToneHint(member);
    return line;
}

std::string ComposeDinerPromptBlock(
    const std::vector<prosophor::nr::domain::Member>& members) {
    std::string block = "<在场成员>";
    if (members.empty()) {
        block += "未识别到已建档的家庭成员（可能是客人）";
    } else {
        for (size_t i = 0; i < members.size(); ++i) {
            if (i > 0) block += "；";
            block += ComposeToneLine(members[i]);
        }
    }
    block += "</在场成员>\n";
    return block;
}

bool VisitorSilence(
    const std::vector<prosophor::nr::domain::MemberBrief>& present_members) {
    return std::none_of(present_members.begin(), present_members.end(),
                        [](const prosophor::nr::domain::MemberBrief& brief) {
                            return !brief.name.empty();
                        });
}

}  // namespace prosophor::nr::app
