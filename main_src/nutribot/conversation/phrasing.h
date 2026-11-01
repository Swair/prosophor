// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// 话术/提示词拼装：所有"对用户说的话"和"注入对话的文本"集中在这里调措辞。
// 只做纯文本格式化，不持有状态、不做 IO；何时说由各领域逻辑决定。
//
// 任务事件/工具说明由 resident_agent.cc / resident_meal.cc 组装（与 agent
// 回合绑定），本文件只负责可复用的成员/语气与餐桌上下文块。

#include <string>
#include <vector>

#include "domain/member.h"

namespace prosophor::nr::app {

/// 按档案压出语气档（儿童 → "活泼鼓励"；糖尿病/高血压/高血脂标签 →
/// "克制提醒，语气温和不吓人"；60 岁及以上 → "温暖放慢"；其余 →
/// "简洁数据"）。慢病标签优先于年龄档。
std::string MemberToneHint(const prosophor::nr::domain::Member& member);

/// 拼装注入对话消息的成员上下文块；末尾带换行，直接拼接在用户文本之前。
/// 空列表 → "未识别到已建档的家庭成员（可能是客人）"。
std::string ComposeDinerPromptBlock(
    const std::vector<prosophor::nr::domain::Member>& members);

}  // namespace prosophor::nr::app
