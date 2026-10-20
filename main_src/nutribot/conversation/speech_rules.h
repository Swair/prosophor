// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace prosophor::nr::app {

/// 这句是在直接问机器人/涉及饮食把关吗（纯函数）。
/// 用于沉默兜底：句含疑问词（问/吗/呢/？）或营养把关词（热量/糖/盐/卡/减肥…）
/// 却输出沉默时，判为"该答不答"，应用侧重问一次（见 nutribot_app 的话轮）。
/// 0.5B 实测：给"可以不接话"的出口就会统统沉默（连"有什么推荐"都不答），
/// 规则兜底保证"被点名必答"，沉默只在规则也认为可沉默时生效。
bool ShouldAnswerUtterance(const std::string& text);

}  // namespace prosophor::nr::app
