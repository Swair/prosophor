// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

// ============================================================================
// UI 通用类型定义 - 放在 common/ 避免循环依赖
// ============================================================================

namespace aicode {

/// Agent 运行时状态
enum class AgentRuntimeState {
    IDLE,
    THINKING,
    EXECUTING_TOOL,
    TOOL_MSG,
    WAITING_PERMISSION,
    STATE_ERROR,
    COMPLETE,
    STREAM_TYPING,
    STREAM_MODE_COMPLETE
};

/// 聊天消息结构
struct ChatMessage {
    std::string role;     // "user", "assistant", "system"
    std::string name;     // 显示的名字（如"代码专家"、"用户"）
    std::string content;  // 消息内容
    double timestamp;
};

/// 状态视觉属性
struct StateVisualProps {
    uint8_t r, g, b, a;
    std::string name;
};

}  // namespace aicode
