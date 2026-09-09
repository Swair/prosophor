// 终端问答适配器（Phase 6 替换为 ASR 听 + TTS 播报；对话用例不变）
//
// 移植自 Python interface/quiz_cli.py 的 TerminalDialogue。
#pragma once

#include <iostream>
#include <optional>
#include <string>

#include "ports.h"

namespace prosophor::nr::app {

class TerminalDialogue : public DialoguePort {
public:
    // 向用户提问并取回答文本（EOF/中断返回 nullopt）
    std::optional<std::string> ask(const std::string& question) override {
        std::cout << "\n🤖 " << question << "\n😀 " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) return std::nullopt;
        // Python input() 语义：去两端空白后空串仍为"已答空"
        const size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        return line.substr(b);
    }
};

}  // namespace prosophor::nr::app
