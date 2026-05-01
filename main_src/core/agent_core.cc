// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/agent_core.h"

#include <chrono>
#include <sstream>
#include <thread>
#include <atomic>

#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"

#include "common/constants.h"
#include "managers/skill_loader.h"
#include "core/compact_service.h"
#include "managers/token_tracker.h"
#include "core/reference_parser.h"
#include "tools/tool_registry.h"

namespace prosophor {

// Truncates a tool result if it exceeds the limit
static std::string TruncateToolResult(const std::string& result,
                                      int max_chars, int keep_lines) {
    if (static_cast<int>(result.size()) <= max_chars) return result;

    std::vector<std::string> lines;
    std::istringstream stream(result);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    if (static_cast<int>(lines.size()) <= keep_lines * 2) {
        return result;
    }

    std::string truncated;
    for (int i = 0; i < keep_lines; ++i) {
        truncated += lines[i] + "\n";
    }

    int omitted = static_cast<int>(lines.size()) - keep_lines * 2;
    truncated += "\n... [" + std::to_string(omitted) + " lines omitted] ...\n\n";
    for (int i = static_cast<int>(lines.size()) - keep_lines; i < static_cast<int>(lines.size()); ++i) {
        truncated += lines[i] + "\n";
    }
    return truncated;
}

/// Set session output (state + state_message + optional reply message)
/// Calls session output callback to notify UI
void AgentCore::SetSessionOutput(AgentSession& session, AgentRuntimeState state,
                                  const std::string& state_msg,
                                  const std::optional<MessageSchema>& reply) {
    session.state = state;
    session.state_message = state_msg;

    // Add message to history if provided (skip streaming intermediate states)
    if (reply && (
        state == AgentRuntimeState::STREAM_MODE_COMPLETE ||
        state == AgentRuntimeState::COMPLETE ||
        state == AgentRuntimeState::TOOL_USE ||
        state == AgentRuntimeState::STATE_ERROR)) {
        session.messages.push_back(*reply);
    }
    
    // Call session output callback if set
    if (session.output_callback) {
        session.output_callback(session.session_id, session.role_id, state, state_msg, reply);
    } else {
        LOG_DEBUG("[SetSessionOutput] output_callback is NOT set!");
    }
}

/// Execute tool calls and build messages - shared by CloseLoop and CloseLoopStream
bool AgentCore::ExecuteToolCalls(const std::vector<ToolUseSchema>& tool_calls,
                                  AgentSession& session,
                                  MessageSchema& assistant_msg,
                                  std::string& accumulated_text,
                                  int& iterations) {
    if (tool_calls.empty()) {
        return false;
    }

    LOG_DEBUG("LLM requested {} tool calls", tool_calls.size());

    // Build results message
    MessageSchema results_msg;
    results_msg.role = "user";
    bool has_tool_error = false;

    for (const auto& tc : tool_calls) {
        // Check for interrupt during tool execution
        if (session.stop_requested) {
            SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, "Interrupted by user");
            throw std::runtime_error("Interrupted by user");
        }

        // Add tool call to assistant message
        assistant_msg.AddToolUseContent(tc.id, tc.name, tc.arguments);
        SetSessionOutput(session, AgentRuntimeState::EXECUTING_TOOL, std::string("Tool using: ") + tc.name + ", args: " + tc.arguments.dump());
        try {
            auto result = session.tool_executor(tc.name, tc.arguments);
            // Only truncate successful results, keep error output intact
            result = TruncateToolResult(result, kToolResultMaxChars, kToolResultKeepLines);
            SetSessionOutput(session, AgentRuntimeState::EXECUTING_TOOL, std::string("Tool using: ") + tc.name + ", result: " + result);
            results_msg.AddToolResultContent(tc.id, result);
        } catch (const std::exception& e) {
            // Tool execution failed - DON'T truncate error message
            // Full error context is critical for LLM to diagnose and fix the issue
            has_tool_error = true;
            std::string error_result = e.what();
            SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, std::string("Tool using: ") + tc.name + ", error_result: " + error_result);
            results_msg.AddToolResultContent(tc.id, error_result);
        }
    }

    // Set EXECUTING_TOOL state and add messages to history
    SetSessionOutput(session, AgentRuntimeState::TOOL_USE, "", assistant_msg);
    SetSessionOutput(session, AgentRuntimeState::TOOL_USE, "", results_msg);

    iterations++;

    // If tool had errors, LLM will see the error and can decide what to do next
    if (has_tool_error) {
        SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, "Tool execution had errors, continuing to let LLM handle");
    }

    // Clear accumulated text after tool execution
    accumulated_text.clear();

    return true;
}

std::string AgentCore::ProcessFileRefs(const std::string& message, const AgentSession& session) {
    std::string processed_message = message;

    if (message.empty() || !ReferenceParser::HasFileRefs(message)) {
        return processed_message;
    }

    auto file_refs = ReferenceParser::ParseFileRefs(message, session.working_directory);

    // Load file contents
    for (auto& ref : file_refs) {
        if (ref.exists) {
            LOG_INFO("Loaded file reference: {}", ref.path);
        } else {
            LOG_WARN("File reference not found: {}", ref.path);
        }
    }

    // Replace @file with actual content
    processed_message = ReferenceParser::ReplaceFileRefs(message, file_refs);

    return processed_message;
}

void AgentCore::MaybeCompact(AgentSession& session) {
    auto& compact_service = CompactService::GetInstance();

    if (!compact_service.NeedsCompaction(session.messages)) {
        return;
    }

    LOG_INFO("Context compaction triggered");

    auto llm_callback = [&session](const std::string& prompt) -> std::string {
        ChatRequest req;
        if (session.role) {
            req.model = session.role->model;
            req.temperature = session.role->temperature;
            req.max_tokens = 4096;
        }
        if (!session.base_url.empty()) {
            req.base_url = session.base_url;
        }
        req.api_key = session.api_key;
        req.timeout = session.timeout;
        req.AddUserMessage(prompt);
        return session.provider->Chat(req).content_text;
    };

    auto compact_result = compact_service.Compact(session.messages, llm_callback);
    session.messages = compact_result.kept_messages;

    // Add summary to system prompt
    if (!compact_result.summary.empty()) {
        session.system_prompt.clear();
        session.system_prompt.push_back({"text", compact_result.summary, false});
    }

    LOG_INFO("Compaction complete: removed {} messages, saved ~{} tokens",
             compact_result.messages_removed, compact_result.tokens_saved);
}

ChatRequest AgentCore::BuildRequest(const AgentSession& session) {
    ChatRequest req;

    // session.role 由 session_manager 在创建/切换时保证非空
    if (!session.role) {
        LOG_FATAL("session.role is null");
        throw std::runtime_error("session.role must not be null");
    }
    if (session.base_url.empty()) {
        LOG_FATAL("session.base_url is empty for session {} (role: {})",
                  session.session_id, session.role_id);
        throw std::runtime_error("session.base_url must not be empty");
    }
    if (session.api_key.empty()) {
        LOG_FATAL("session.api_key is empty for session {} (role: {}, provider: '{}')",
                  session.session_id, session.role_id,
                  session.role ? session.role->provider_prot : "N/A");
        throw std::runtime_error(
            "API key is empty. Please check your provider config in settings.json "
            "(api_key field) or set the corresponding environment variable.");
    }
    if (session.timeout <= 0) {
        LOG_WARN("session.timeout is invalid ({}), using default 60s", session.timeout);
        req.timeout = 60;
    } else {
        req.timeout = session.timeout;
    }
    req.model = session.role->model;
    req.temperature = session.role->temperature;
    req.max_tokens = session.role->max_tokens;
    req.thinking = session.role->thinking;
    req.base_url = session.base_url;
    req.api_key = session.api_key;

    req.messages = session.messages;
    req.system = session.system_prompt;
    // 根据 role.tools 是否为空来判断是否发送工具（tools_white_list 字段配置了才发送）
    req.tools = session.use_tools && session.role && !session.role->tools.empty()
                ? session.role->tools : std::vector<ToolsSchema>{};
    req.tool_choice_auto = session.use_tools && session.role && !session.role->tools.empty();

    LOG_DEBUG("BuildRequest: use_tools={}, role->tools.size()={}, req.tools.size()={}",
             session.use_tools,
             session.role ? session.role->tools.size() : 0,
             req.tools.size());
    LOG_DEBUG("BuildRequest: model='{}', base_url='{}', timeout={}s, api_key={}",
             req.model, req.base_url, req.timeout,
             req.api_key.size() > 8 ? req.api_key.substr(0, 8) + "..." : req.api_key);
    LOG_DEBUG("BuildRequest: thinking='{}', temperature={}, max_tokens={}",
             req.thinking, req.temperature, req.max_tokens);
    return req;
}

int AgentCore::GetMaxIterations(const AgentSession& session) {
    return session.role ? session.role->max_iterations : 15;
}

void AgentCore::Loop(const std::string& message, AgentSession& session) {
    // Determine streaming mode from role configuration
    bool streaming = session.role && session.role->enable_streaming;
    LOG_DEBUG("Processing message (streaming={})", streaming);

    // Set initial THINKING state
    SetSessionOutput(session, AgentRuntimeState::BEGINNING, "Processing...");

    // Process message - resolve @file references
    std::string processed_message = ProcessFileRefs(message, session);

    // Add user message (with resolved references)
    if (!processed_message.empty()) {
        session.messages.emplace_back("user", processed_message);
    }

    // Check if compaction is needed
    MaybeCompact(session);

    int iterations = 0;
    int max_iterations = GetMaxIterations(session);

    while (iterations < max_iterations && !session.stop_requested) {
        ChatRequest request = BuildRequest(session);
        request.stream = streaming;

        // Call LLM - streaming or non-streaming
        ChatResponse response;
        if (streaming) {
            response = session.provider->ChatStream(
                request, [&session](const ChatResponse& chunk) {
                    if (!chunk.thinking_phase.empty()) {
                        if (chunk.thinking_phase == "start") {
                            SetSessionOutput(session, AgentRuntimeState::STREAM_THINKING_START, "", std::nullopt);
                        } else if (chunk.thinking_phase == "delta") {
                            MessageSchema thinking_msg;
                            thinking_msg.role = "assistant";
                            thinking_msg.AddThinkingContent(chunk.content_thinking);
                            SetSessionOutput(session, AgentRuntimeState::STREAM_THINKING, "", thinking_msg);
                        } else if (chunk.thinking_phase == "end") {
                            SetSessionOutput(session, AgentRuntimeState::STREAM_THINKING_END, "", std::nullopt);
                        }
                    } else if (!chunk.content_phase.empty()) {
                        if (chunk.content_phase == "start") {
                            SetSessionOutput(session, AgentRuntimeState::STREAM_CONTENT_START, "", std::nullopt);
                        } else if (chunk.content_phase == "delta") {
                            MessageSchema chunk_msg;
                            chunk_msg.role = "assistant";
                            chunk_msg.AddTextContent(chunk.content_text);
                            SetSessionOutput(session, AgentRuntimeState::STREAM_CONTENT_TYPING, "", chunk_msg);
                        } else if (chunk.content_phase == "end") {
                            SetSessionOutput(session, AgentRuntimeState::STREAM_CONTENT_END, "", std::nullopt);
                        }
                    }
                });
        } else {
            response = session.provider->Chat(request);
        }

        // Build assistant message with thinking (if any)
        MessageSchema assistant_msg;
        assistant_msg.role = "assistant";
        if (!response.content_thinking.empty()) {
            assistant_msg.AddThinkingContent(response.content_thinking);
        }

        // Execute tool calls if present
        if (ExecuteToolCalls(response.tool_calls, session, assistant_msg, response.content_text, iterations)) {
            continue;
        }

        // No tool calls - check for text response
        if (!response.content_text.empty()) {
            assistant_msg.AddTextContent(response.content_text);

            if (streaming) {
                SetSessionOutput(session, AgentRuntimeState::STREAM_MODE_COMPLETE, "Done.", assistant_msg);
            } else {
                SetSessionOutput(session, AgentRuntimeState::COMPLETE, "Done.", assistant_msg);
            }
            return;
        }

        if (session.stop_requested) {
            assistant_msg.role = "assistant";
            assistant_msg.AddTextContent("[Agent turn stopped by user]");
            SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, "Stopped by user", assistant_msg);
            return;
        }

        SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, "Unexpected LLM response format");
        break;
    }

    // Max iterations or stopped
    std::string stop_text =
        session.stop_requested ? "[Stopped]" : "[Max iterations reached]";
    SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, stop_text);
    throw std::runtime_error("Failed to get valid response after " +
                             std::to_string(max_iterations) + " iterations");
}

}  // namespace prosophor
