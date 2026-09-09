// 用例：对话式画像采集 —— 机器人逐题问答获取忌口/饮食习惯/偏好
//
// 移植自 Python application/profile_quiz.py（语义 1:1，可脱离硬件单测）。
//  - 问题集/解析规则与应用无关（TTS 播报、终端打印都走 DialoguePort）
//  - 否定回答（没有/无/不挑食…）→ 不覆盖（新成员为空，老成员保留原值）
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "member.h"
#include "ports.h"

namespace prosophor::nr::app {

// 问卷问题定义（text / int / csv 三种作答类型）
struct QuizQuestion {
    std::string key;
    std::string text;
    std::string kind = "csv";     // text / int / csv
    std::string help;
};

// 回答 -> 标签列表：否定/空白 -> 空；"少盐 爱吃辣" -> ['少盐','爱吃辣']
std::vector<std::string> parse_csv_answer(const std::string& answer);

// 画像问卷用例：逐题问答 -> 生成/更新 Member 画像
class ProfileQuiz {
public:
    // 完整问卷（8 题；与 Python QUESTIONS 顺序一致）
    static const std::vector<QuizQuestion>& questions();

    // existing 为已建档成员时：未答项保留原值（含 member_id/人脸特征）
    explicit ProfileQuiz(DialoguePort& dialogue,
                         std::optional<nr::domain::Member> existing = std::nullopt);

    // 执行问卷；返回完成画像的 Member（未含人脸特征，由拍照环节补充）；
    // 中途无回应或最终无姓名返回 nullopt
    std::optional<nr::domain::Member> Run();

private:
    std::optional<std::string> Ask(const QuizQuestion& q);

    DialoguePort* dialogue_;
    std::optional<nr::domain::Member> existing_;
};

}  // namespace prosophor::nr::app
