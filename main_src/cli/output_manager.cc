// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "cli/output_manager.h"
#include "common/log_wrapper.h"
#include <iostream>

namespace aicode {

OutputManager& OutputManager::GetInstance() {
    static OutputManager instance;
    return instance;
}

void OutputManager::Output(const std::string& text) {
    if (mode_ == Mode::Terminal) {
        std::cout << "\n" << text << "\n\n" << std::flush;
    } else {
        // SDL 模式：通过回调通知 UI
        if (state_callback_) {
            state_callback_(0, text);  // state=0 表示普通输出
        }
    }
}

void OutputManager::StreamOutput(const OutputEvent& event) {
    if (mode_ == Mode::Terminal) {
        switch (event.type) {
            case OutputEvent::Type::TextDelta:
                std::cout << event.content << std::flush;
                break;
            case OutputEvent::Type::Error:
                std::cerr << "Error: " << event.content << std::endl;
                break;
            default:
                // 其他事件类型在终端模式下简化处理
                break;
        }
    } else {
        // SDL 模式：通过回调通知 UI
        if (state_callback_) {
            state_callback_(event.state_code, event.content);
        }
    }
}

void OutputManager::ShowError(const std::string& error) {
    if (mode_ == Mode::Terminal) {
        std::cerr << "Error: " << error << std::endl;
    } else {
        if (state_callback_) {
            state_callback_(-1, error);  // -1 表示错误状态
        }
    }
}

void OutputManager::ShowPermissionPrompt(const std::string& tool_name, const std::string& command) {
    if (mode_ == Mode::Terminal) {
        std::cout << "\n[Permission Required]" << std::endl;
        std::cout << "Tool: " << tool_name << std::endl;
        std::cout << "Command: " << command << std::endl;
        std::cout << "Do you want to allow this operation? (Y/N or y/n/a for always): " << std::flush;
    } else {
        // SDL 模式：UI 层会显示权限对话框
        if (state_callback_) {
            state_callback_(-2, "Permission required: " + tool_name);  // -2 表示等待权限
        }
    }
}

}  // namespace aicode
