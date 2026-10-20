// 注册闭环：问答 -> 落盘 -> 可读回（对应 Python quiz_cli.py main 全流程）
#include <deque>
#include <filesystem>
#include <optional>
#include <string>

#include "storage/member_repository.h"
#include "micro_test.hpp"
#include "ports.h"
#include "conversation/quiz_cli.h"

using namespace prosophor::nr::app;
using namespace prosophor::nr::infra;

namespace {

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

TEST(register, new_member_quiz_persisted) {
    const std::string dir = "/tmp/nr_reg_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    JsonMemberRepository repo(dir);

    FakeDialogue dlg;
    dlg.answers = {"小明", "6", "男", "牛奶", "没有", "少盐", "清淡", "儿童"};
    const auto m = RunQuizCli(repo, std::nullopt, dlg);

    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(m->name, "小明");
    // 已落盘可读回
    const auto back = repo.get_member("小明");
    EXPECT_TRUE(back.has_value());
    EXPECT_TRUE(back->profile.age.has_value() && *back->profile.age == 6);
    EXPECT_EQ(back->profile.taboo_foods[0], "牛奶");
    std::filesystem::remove_all(dir);
}

TEST(register, abort_without_name_persists_nothing) {
    const std::string dir = "/tmp/nr_reg_test2";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    JsonMemberRepository repo(dir);

    FakeDialogue dlg;   // 全程无回应
    EXPECT_FALSE(RunQuizCli(repo, std::nullopt, dlg).has_value());
    EXPECT_TRUE(repo.list_members().empty());
    std::filesystem::remove_all(dir);
}

TEST(register, existing_member_completion_keeps_fields) {
    const std::string dir = "/tmp/nr_reg_test3";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    JsonMemberRepository repo(dir);

    // 先建档（含特征）
    prosophor::nr::domain::Member seed;
    seed.member_id = "id-x";
    seed.name = "小明";
    seed.profile.age = 5;
    seed.profile.taboo_foods = {"牛奶"};
    seed.face_embeddings = {{0.1f}};
    repo.add_member(seed);

    // 补全问卷：age 更新为 6，其余未答保留
    FakeDialogue dlg;
    dlg.answers = {"小明", "6", "", "", "", "", "", ""};
    const auto m = RunQuizCli(repo, std::string("小明"), dlg);

    EXPECT_TRUE(m.has_value());
    EXPECT_TRUE(m->profile.age.has_value() && *m->profile.age == 6);
    EXPECT_EQ(m->profile.taboo_foods.size(), 1u);       // 保留
    EXPECT_EQ(m->face_embeddings.size(), 1u);           // 特征保留
    EXPECT_EQ(m->member_id, "id-x");
    std::filesystem::remove_all(dir);
}
