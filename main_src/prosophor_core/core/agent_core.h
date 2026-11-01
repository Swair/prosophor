// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/messages_schema.h"
#include "core/agent_session.h"
#include "core/agent_types.h"

namespace prosophor {

/// AgentCore: orchestrates message processing, tool execution, and LLM interaction
/// Stateless utility class - all state is in AgentSession
class AgentCore {
 public:
    /// Process a message - streaming mode is determined by session.role->enable_streaming
    /// @param message User message
    /// @param session Agent session (read/write) - contains tool_executor, stop_requested, role
    /// 本轮结果通过 session 表达：
    /// - 成功：最后一条 assistant 消息即本轮正文（调用方先看 state != STATE_ERROR 再读）
    /// - 失败：SetOutput(STATE_ERROR, ...)，调用方不要读消息列表
    static void Loop(const std::string& message, AgentSession& session);

 private:
    /// Validate session state before building request (role, base_url, api_key)
    static void ValidateSession(const AgentSession& session);

    /// Build ChatRequest from AgentSession
    static ChatRequest BuildRequest(const AgentSession& session);

    /// Process @file references in user message
    static std::string ProcessFileRefs(const std::string& message, const AgentSession& session);

    /// Extract [摘要] from LLM response into assistant_msg.summary
    static void ExtractDialogSummary(const std::string& response_text, MessageSchema& assistant_msg);

    /// Get max iterations from role or default
    static int GetMaxIterations(const AgentSession& session);

    /// Execute tool calls and append to the given assistant message.
    /// 调用方保证 tool_calls 非空；返回 true = 全部工具执行成功。
    static bool ExecuteToolCalls(const std::vector<ToolUseSchema>& tool_calls,
                                 AgentSession& session,
                                 MessageSchema& assistant_msg,
                                 std::string& accumulated_text,
                                 int& iterations);

    /// Trigger memory consolidation if message count exceeds threshold
    static void ConsolidateMemoryIfNeeded(AgentSession& session);
};

}  // namespace prosophor
