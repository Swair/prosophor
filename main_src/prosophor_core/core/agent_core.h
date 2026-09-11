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
    /// @return 本轮最终助手回复全文；未产出回复（被打断、API 错误、工具轮耗尽
    ///         max_iterations）时为空 —— 调用方据此区分"没答"与"答了空白"。
    ///         刻意不由消息列表末条反推：失败路径下 CleanupInterruptedLoop 会 pop
    ///         掉孤立的 user 消息，末条会退回上一轮的回复，造成静默串答。
    static std::string Loop(const std::string& message, AgentSession& session);

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

    /// Execute tool calls and append to the given assistant message
    static bool ExecuteToolCalls(const std::vector<ToolUseSchema>& tool_calls,
                                 AgentSession& session,
                                 MessageSchema& assistant_msg,
                                 std::string& accumulated_text,
                                 int& iterations);

    /// Trigger memory consolidation if message count exceeds threshold
    static void ConsolidateMemoryIfNeeded(AgentSession& session);
};

}  // namespace prosophor
