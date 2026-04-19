// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "common/messages_schema.h"
#include "core/agent_session.h"
#include "core/compact_service.h"  // For GetCompactService()
#include "components/ui_types.h"  // For AgentRuntimeState

namespace aicode {

/// AgentCore: orchestrates message processing, tool execution, and LLM interaction
/// Stateless utility class - all state is in AgentSession
class AgentCore {
 public:
    /// Process a message - streaming mode is determined by session.role->enable_streaming
    /// @param message User message
    /// @param session Agent session (read/write) - contains tool_executor, stop_requested, role
    static void Loop(const std::string& message, AgentSession& session);

    /// Get compact service for context compaction
    static CompactService& GetCompactService() { return CompactService::GetInstance(); }

 private:
    /// Build ChatRequest from AgentSession
    static ChatRequest BuildRequest(const AgentSession& session);

    /// Process @file references in user message
    static std::string ProcessFileRefs(const std::string& message, const AgentSession& session);

    /// Check and perform context compaction if needed
    static void MaybeCompact(AgentSession& session);

    /// Get max iterations from role or default
    static int GetMaxIterations(const AgentSession& session);

    /// Set session output (state + state_message + optional reply message)
    /// Calls session output callback to notify UI
    static void SetSessionOutput(AgentSession& session, AgentRuntimeState state,
                                 const std::string& state_msg,
                                 const std::optional<MessageSchema>& reply = std::nullopt);

    /// Execute tool calls and build messages
    /// Returns true if tool calls were executed, false if no tool calls
    static bool ExecuteToolCalls(const std::vector<ToolUseSchema>& tool_calls,
                                 AgentSession& session,
                                 std::string& accumulated_text,
                                 int& iterations);
};

}  // namespace aicode
