// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <variant>
#include <sstream>

namespace prosophor {

/// 输入源类型
enum class InputSource {
    Terminal,  // 终端输入
    SDL,       // SDL UI 输入
};

/// 文本输入事件
struct TextInputEvent {
    std::string text;
    bool is_complete = true;  // 多行输入时使用
};

/// 命令输入事件
struct CommandInputEvent {
    std::string command;       // 命令名（不含 '/'）
    std::vector<std::string> args;
};

/// 鼠标事件
struct MouseEvent {
    enum Type { Click, DoubleClick, Drag };
    Type type;
    int x, y;
    int button;  // 0=left, 1=right, 2=middle
};

/// 键盘事件
struct KeyEvent {
    int keycode;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

/// 统一的输入事件
struct InputEvent {
    InputSource source;

    /// 事件类型
    enum class Type {
        Text,       // 文本输入
        Command,    // 命令（/xxx）
        Mouse,      // 鼠标事件
        Key,        // 键盘事件
        Interrupt,  // 中断（Ctrl+C / ESC）
    };
    Type type;

    /// 事件数据（互斥 variant）
    std::variant<
        TextInputEvent,
        CommandInputEvent,
        MouseEvent,
        KeyEvent
    > data;

    // 辅助方法
    bool IsText() const { return type == Type::Text; }
    bool IsCommand() const { return type == Type::Command; }
    bool IsInterrupt() const { return type == Type::Interrupt; }
    bool IsMouse() const { return type == Type::Mouse; }
    bool IsKey() const { return type == Type::Key; }

    std::string GetText() const {
        if (type == Type::Text) {
            return std::get<TextInputEvent>(data).text;
        }
        return "";
    }

    CommandInputEvent GetCommand() const {
        if (type == Type::Command) {
            return std::get<CommandInputEvent>(data);
        }
        return {"", {}};
    }

    /// 重建命令字符串（用于日志）
    std::string GetCommandText() const {
        if (type != Type::Command) return "";
        auto cmd = GetCommand();
        std::ostringstream oss;
        oss << "/" << cmd.command;
        for (const auto& arg : cmd.args) {
            oss << " " << arg;
        }
        return oss.str();
    }
};

}  // namespace prosophor
