// diner_context 单测：语气档路由、儿童直呼、提示块拼装、访客静默判据。
// 全纯函数，直接喂档案构造——不碰仓储、不碰感知。
#include "conversation/diner_context.h"

#include <string>
#include <vector>

#include "domain/events.h"
#include "domain/member.h"
#include "micro_test.hpp"

namespace {

using prosophor::nr::app::ComposeDinerPromptBlock;
using prosophor::nr::app::DirectAddress;
using prosophor::nr::app::MemberToneHint;
using prosophor::nr::app::VisitorSilence;
using prosophor::nr::domain::Member;
using prosophor::nr::domain::MemberBrief;

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

Member MakeMember(const std::string& name, std::optional<int> age,
                  std::vector<std::string> tags = {}) {
    Member member;
    member.member_id = "id_" + name;
    member.name = name;
    member.profile.age = age;
    member.profile.health_tags = std::move(tags);
    return member;
}

MemberBrief MakeBrief(const std::string& name) {
    MemberBrief brief;
    brief.name = name;
    return brief;
}

}  // namespace

// ── MemberToneHint：档案 -> 语气档 ──

TEST(diner_context, tone_child_is_playful) {
    EXPECT_EQ(MemberToneHint(MakeMember("小明", 6, {"儿童"})), std::string("活泼鼓励"));
    EXPECT_EQ(MemberToneHint(MakeMember("小红", 11)), std::string("活泼鼓励"));
}

TEST(diner_context, tone_elder_is_warm) {
    EXPECT_EQ(MemberToneHint(MakeMember("王奶奶", 68)), std::string("温暖放慢"));
}

TEST(diner_context, tone_chronic_tag_beats_age) {
    // 慢病标签优先于年龄档：重点在"忌口多、要克制"
    const std::string hint = MemberToneHint(MakeMember("王奶奶", 68, {"2型糖尿病", "老年人"}));
    EXPECT_TRUE(Contains(hint, "克制"));
    EXPECT_TRUE(Contains(hint, "温和"));
}

TEST(diner_context, tone_adult_is_concise) {
    EXPECT_EQ(MemberToneHint(MakeMember("爸爸", 35)), std::string("简洁数据"));
    EXPECT_EQ(MemberToneHint(MakeMember("宝宝", std::nullopt)), std::string("简洁数据"));
}

// ── DirectAddress：儿童优先的直呼名 ──

TEST(diner_context, address_prefers_child) {
    const std::vector<Member> members = {MakeMember("爸爸", 35), MakeMember("小明", 6)};
    EXPECT_EQ(DirectAddress(members), std::string("小明"));
}

TEST(diner_context, address_skips_adults) {
    const std::vector<Member> members = {MakeMember("爸爸", 35), MakeMember("王奶奶", 68)};
    EXPECT_EQ(DirectAddress(members), std::string(""));
}

TEST(diner_context, address_empty_for_no_members) {
    EXPECT_EQ(DirectAddress({}), std::string(""));
}

// ── ComposeDinerPromptBlock：注入对话消息的成员上下文块 ──

TEST(diner_context, block_reports_unknown_visitors) {
    const std::string block = ComposeDinerPromptBlock({});
    EXPECT_TRUE(Contains(block, "<在场成员>"));
    EXPECT_TRUE(Contains(block, "未识别到已建档"));
    EXPECT_TRUE(Contains(block, "</在场成员>"));
    EXPECT_TRUE(Contains(block, "\n"));   // 末尾换行：直接拼在用户文本前
}

TEST(diner_context, block_formats_member_with_age_and_tags) {
    const std::string block =
        ComposeDinerPromptBlock({MakeMember("小明", 6, {"儿童", "乳糖不耐受"})});
    EXPECT_TRUE(Contains(block, "小明（6岁，儿童、乳糖不耐受）：活泼鼓励"));
}

TEST(diner_context, block_joins_multiple_members_deterministically) {
    const std::string block = ComposeDinerPromptBlock(
        {MakeMember("小明", 6, {"儿童"}), MakeMember("王奶奶", 68, {"2型糖尿病"})});
    EXPECT_TRUE(Contains(block, "小明（6岁，儿童）：活泼鼓励"));
    EXPECT_TRUE(Contains(block, "王奶奶（68岁，2型糖尿病）："));
    EXPECT_TRUE(Contains(block, "克制"));
}

TEST(diner_context, block_handles_member_without_age) {
    const std::string block = ComposeDinerPromptBlock({MakeMember("阿姨", std::nullopt, {"素食"})});
    EXPECT_TRUE(Contains(block, "阿姨（素食）：简洁数据"));
}

// ── VisitorSilence：本帧无注册成员 -> 主动播报静默 ──

TEST(diner_context, visitor_silence_when_no_members) {
    EXPECT_TRUE(VisitorSilence({}));
}

TEST(diner_context, visitor_silence_when_only_unknown_faces) {
    EXPECT_TRUE(VisitorSilence({MakeBrief("")}));
}

TEST(diner_context, no_silence_when_member_recognized) {
    EXPECT_FALSE(VisitorSilence({MakeBrief("") , MakeBrief("小明")}));
}
