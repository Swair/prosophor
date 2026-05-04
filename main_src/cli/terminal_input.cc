// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "cli/input_manager.h"
#include "cli/input_handler.h"
#include "common/log_wrapper.h"
#include <thread>
#include <sstream>
#include <mutex>
#include <iostream>

namespace prosophor {

class TerminalInput : public InputManager {
public:
    TerminalInput() : input_handler_(std::make_unique<InputHandler>()) {
    }

    void Start() override {
        running_ = true;
        input_thread_ = std::thread([this]() {
            LOG_INFO("TerminalInput: starting input loop");
            std::cout << "> " << std::flush;
            while (running_) {
                std::string line = input_handler_->ReadLine("");
                std::cout << "> " << std::flush;

                if (line.empty()) {
                    // 空行（回车）跳过，继续输入
                    // EOF (Ctrl+D) 也在 ReadLine 中处理了
                    continue;
                }

                // 判断是否是命令
                if (!line.empty() && line[0] == '/') {
                    auto [cmd, args] = ParseCommand(line);
                    if (callback_) {
                        callback_(InputEvent{
                            InputSource::Terminal,
                            InputEvent::Type::Command,
                            CommandInputEvent{cmd, args}
                        });
                    }
                } else {
                    // 普通文本
                    if (callback_) {
                        callback_(InputEvent{
                            InputSource::Terminal,
                            InputEvent::Type::Text,
                            TextInputEvent{line}
                        });
                    }
                }
            }
            LOG_INFO("TerminalInput: input loop ended");
        });
    }

    std::unique_ptr<InputHandler> input_handler_;
    std::thread input_thread_;
    std::atomic<bool> running_{false};

    void Stop() override {
        if (!running_) return;

        running_ = false;
        input_handler_->DisableRawMode();
        if (input_thread_.joinable()) {
            input_thread_.join();
        }
    }

    bool IsRunning() const override {
        return running_;
    }

    std::pair<std::string, std::vector<std::string>> ParseCommand(const std::string& line) {
        // 解析 /cmd arg1 arg2
        std::istringstream iss(line.substr(1));  // 跳过 '/'
        std::string cmd;
        iss >> cmd;

        std::vector<std::string> args;
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }

        return {cmd, args};
    }
};

std::unique_ptr<InputManager> CreateTerminalInput() {
    return std::make_unique<TerminalInput>();
}

}  // namespace prosophor
