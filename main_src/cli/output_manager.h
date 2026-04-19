// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/input_event.h"
#include "common/noncopyable.h"
#include <string>
#include <functional>

namespace aicode {

/// 输出管理器 - 统一处理所有输出
class OutputManager : public Noncopyable {
public:
    static OutputManager& GetInstance();

    enum class Mode {
        Terminal,
        SDL
    };

    void SetMode(Mode mode) { mode_ = mode; }
    Mode GetMode() const { return mode_; }

    /// 输出文本
    void Output(const std::string& text);

    /// 流式输出
    void StreamOutput(const OutputEvent& event);

    /// 显示错误
    void ShowError(const std::string& error);

    /// 显示权限提示
    void ShowPermissionPrompt(const std::string& tool_name, const std::string& command);

    /// 设置状态变化回调（SDL 模式使用）
    using StateChangeCallback = std::function<void(int state, const std::string& message)>;
    void SetStateChangeCallback(StateChangeCallback cb) { state_callback_ = cb; }

private:
    OutputManager() = default;
    ~OutputManager() = default;

    Mode mode_ = Mode::Terminal;
    StateChangeCallback state_callback_;
};

}  // namespace aicode
