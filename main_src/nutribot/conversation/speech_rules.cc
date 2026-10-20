// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "conversation/speech_rules.h"

namespace prosophor::nr::app {

bool ShouldAnswerUtterance(const std::string& text) {
    // 问询词：出现即"在问话"
    for (const char* token : {"？", "?", "吗", "呢", "怎么", "多少", "什么", "推荐"}) {
        if (text.find(token) != std::string::npos) return true;
    }
    // 营养把关词：即使陈述句，"这汤太咸了"也该接（餐桌管家职责内）
    for (const char* token : {"热量", "千卡", "卡路里", "糖", "盐", "咸", "油",
                              "减肥", "禁忌", "过敏", "能吃", "不能吃", "可以吃"}) {
        if (text.find(token) != std::string::npos) return true;
    }
    return false;
}

}  // namespace prosophor::nr::app
