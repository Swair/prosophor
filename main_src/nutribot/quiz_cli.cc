// RunQuizCli 实现 —— 对照 Python interface/quiz_cli.py main()
#include "quiz_cli.h"

#include "profile_quiz.h"

namespace prosophor::nr::app {

std::optional<nr::domain::Member> RunQuizCli(
    nr::infra::JsonMemberRepository& repo,
    const std::optional<std::string>& existing_name, DialoguePort& dialogue) {
    // 已有成员：取出档案做补全基准（不存在则按新成员建档）
    std::optional<nr::domain::Member> existing = std::nullopt;
    if (existing_name.has_value()) {
        existing = repo.get_member(*existing_name);
    }

    ProfileQuiz quiz(dialogue, std::move(existing));
    auto member = quiz.Run();
    if (!member.has_value()) {
        return std::nullopt;   // 未获取到姓名，问卷中止
    }

    repo.add_member(*member);
    return member;
}

}  // namespace prosophor::nr::app
