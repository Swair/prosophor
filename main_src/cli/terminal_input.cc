// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "cli/input_manager.h"
#include "cli/input_handler.h"
#include "common/log_wrapper.h"
#include "core/agent_state_visualizer.h"
#include <thread>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <iostream>

namespace aicode {

class TerminalInput : public InputManager {
public:
    TerminalInput() : input_handler_(std::make_unique<InputHandler>()) {
        // 设置状态回调，在输入框下方显示状态
        state_callback_ = [this](AgentRuntimeState state, const std::string& msg) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_state_ = state;
            current_state_msg_ = msg;
            state_dirty_ = true;
            // 刷新显示
            RefreshStateDisplay();
        };
    }

    void Start() override {
        running_ = true;
        input_thread_ = std::thread([this]() {
            LOG_INFO("TerminalInput: starting input loop");
            while (running_) {
                std::string line = input_handler_->ReadLine(" ");

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

private:
    /// 刷新状态显示（在输入框下方右下角）
    void RefreshStateDisplay() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!state_dirty_) return;

        // 构建状态字符串
        std::string state_str = GetStateString(current_state_);
        std::string status = " " + state_str + ": " + current_state_msg_ + " ";

        // 右下角显示，使用简洁边框
        std::cout << "\r\033[K"  // 清除当前行
                  << "\033[90G"  // 移动到第 90 列（右侧）
                  << "\033[2m"   // 半亮/灰色
                  << "╭" << ansi::kBoxHorizontal << ansi::kBoxHorizontal
                  << status
                  << ansi::kBoxHorizontal << ansi::kBoxHorizontal << "╮"
                  << "\033[0m"   // 重置
                  << std::flush;

        state_dirty_ = false;
    }

    std::string GetStateString(AgentRuntimeState state) {
        switch (state) {
            case AgentRuntimeState::THINKING: return "💭 思考";
            case AgentRuntimeState::EXECUTING_TOOL: return "🛠️ 执行";
            case AgentRuntimeState::COMPLETE: return "✓ 完成";
            case AgentRuntimeState::STATE_ERROR: return "✗ 错误";
            default: return "待机";
        }
    }

    std::unique_ptr<InputHandler> input_handler_;
    std::thread input_thread_;
    std::atomic<bool> running_{false};

    // 状态显示
    std::mutex state_mutex_;
    AgentRuntimeState current_state_ = AgentRuntimeState::IDLE;
    std::string current_state_msg_;
    bool state_dirty_ = false;

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

}  // namespace aicode
