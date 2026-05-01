// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

// ============================================================================
// UI 通用类型定义 - 放在 common/ 避免循环依赖
// ============================================================================

namespace prosophor {

/// Agent 运行时状态
enum class AgentRuntimeState {
    IDLE,
    BEGINNING,
    EXECUTING_TOOL,
    TOOL_USE,
    WAITING_PERMISSION,
    STATE_ERROR,
    COMPLETE,
    STREAM_CONTENT_TYPING,      // 流式响应中
    STREAM_MODE_COMPLETE,   // 流式响应完成
    STREAM_THINKING_START,  // 流式思考开始
    STREAM_THINKING,        // 流式思考中
    STREAM_THINKING_END,    // 流式思考结束
    STREAM_CONTENT_START,   // 流式内容开始
    STREAM_CONTENT_END      // 流式内容结束
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

}  // namespace prosophor
