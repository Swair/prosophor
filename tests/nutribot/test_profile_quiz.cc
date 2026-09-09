// ProfileQuiz 用例测试：FakeDialogue 注入，逐题问答验证画像构建
#include <deque>
#include <string>
#include <vector>

#include "micro_test.hpp"
#include "profile_quiz.h"

using namespace prosophor::nr::app;
using prosophor::nr::domain::Member;

namespace {

// Fake 对话端口：顺序消费预置答案队列；耗尽后返回 nullopt（无回应）
class FakeDialogue : public DialoguePort {
public:
    std::deque<std::optional<std::string>> answers;

    std::optional<std::string> ask(const std::string&) override {
        if (answers.empty()) return std::nullopt;
        auto a = std::move(answers.front());
        answers.pop_front();
        return a;
    }
};

}  // namespace

// ---- parse_csv_answer ----
TEST(quiz, csv_split_and_negative) {
    EXPECT_EQ(parse_csv_answer("少盐 爱吃辣").size(), 2u);
    EXPECT_EQ(parse_csv_answer("牛奶、辣椒").size(), 2u);
    EXPECT_EQ(parse_csv_answer("花生;虾 蟹").size(), 3u);
    EXPECT_TRUE(parse_csv_answer("").empty());
    EXPECT_TRUE(parse_csv_answer("没有").empty());       // 否定词
    EXPECT_TRUE(parse_csv_answer("不挑食").empty());
    EXPECT_TRUE(parse_csv_answer("不过敏").empty());
}

// ---- 新成员完整画像 ----
TEST(quiz, new_member_full_profile) {
    FakeDialogue dlg;
    dlg.answers = {"小明", "6", "男", "牛奶、花生", "没有",
                   "少盐", "清淡", "儿童"};
    ProfileQuiz quiz(dlg);
    const auto m = quiz.Run();
    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(m->name, "小明");
    EXPECT_TRUE(m->profile.age.has_value() && *m->profile.age == 6);
    EXPECT_EQ(m->profile.gender, "男");
    EXPECT_EQ(m->profile.taboo_foods.size(), 2u);
    EXPECT_EQ(m->profile.taboo_foods[0], "牛奶");
    EXPECT_TRUE(m->profile.allergies.empty());       // 否定 -> 空
    EXPECT_EQ(m->profile.eating_habits.size(), 1u);
    EXPECT_EQ(m->profile.preferences[0], "清淡");
    EXPECT_EQ(m->profile.health_tags[0], "儿童");
}

// ---- int 越界 -> 年龄置空 ----
TEST(quiz, age_out_of_range_cleared) {
    FakeDialogue dlg;
    dlg.answers = {"小红", "200", "女"};
    ProfileQuiz quiz(dlg);
    const auto m = quiz.Run();
    EXPECT_TRUE(m.has_value());
    EXPECT_FALSE(m->profile.age.has_value());
}

// ---- 已有成员：无回应全部跳过 -> 保留原档案 ----
TEST(quiz, existing_member_preserved_when_no_answer) {
    Member existing;
    existing.member_id = "id-x";
    existing.name = "老张";
    existing.profile.age = 8;
    existing.profile.taboo_foods = {"牛奶"};
    existing.face_embeddings = {{1.0f, 2.0f}};

    FakeDialogue dlg;   // 全程无回应
    ProfileQuiz quiz(dlg, existing);
    const auto m = quiz.Run();
    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(m->member_id, "id-x");
    EXPECT_EQ(m->name, "老张");
    EXPECT_TRUE(m->profile.age.has_value() && *m->profile.age == 8);
    EXPECT_EQ(m->profile.taboo_foods.size(), 1u);
    EXPECT_EQ(m->face_embeddings.size(), 1u);   // 人脸特征保留
}

// ---- 已有成员：否定回答 -> 保留原禁忌列表 ----
TEST(quiz, existing_member_negative_keeps_taboos) {
    Member existing;
    existing.member_id = "id-y";
    existing.name = "老王";
    existing.profile.taboo_foods = {"花生"};

    FakeDialogue dlg;
    dlg.answers = {"", "", "", "没有", "", "", "", ""};   // taboo 答"没有"
    ProfileQuiz quiz(dlg, existing);
    const auto m = quiz.Run();
    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(m->profile.taboo_foods.size(), 1u);   // 否定不覆盖 -> 保留
    EXPECT_EQ(m->profile.taboo_foods[0], "花生");
}

// ---- 无姓名 -> 返回空 ----
TEST(quiz, no_name_returns_null) {
    FakeDialogue dlg;
    dlg.answers = {"", "6"};
    ProfileQuiz quiz(dlg);
    EXPECT_FALSE(quiz.Run().has_value());
}
