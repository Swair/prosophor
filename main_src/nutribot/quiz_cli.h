// 对话式画像采集注册流程（移植自 Python interface/quiz_cli.py main()）
//
// 编排：已有档案(可选) -> ProfileQuiz 逐题问答 -> 落盘(JsonMemberRepository)
// 纯逻辑可测（对话端口注入；Phase 6 换 ASR/TTS 后本流程不变）。
#pragma once

#include <optional>
#include <string>

#include "member.h"
#include "member_repository.h"
#include "ports.h"

namespace prosophor::nr::app {

// 执行画像问卷并落盘；existing_name 非空表示补全已有成员画像（未答项保留）。
// 返回落盘后的 Member；问卷中止（无姓名）返回 nullopt。
std::optional<nr::domain::Member> RunQuizCli(
    nr::infra::JsonMemberRepository& repo,
    const std::optional<std::string>& existing_name, DialoguePort& dialogue);

}  // namespace prosophor::nr::app
