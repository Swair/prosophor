// Copyright 2026 AiCode Contributors
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

namespace aicode {

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

    // Add message to history if provided
    if (reply && state != AgentRuntimeState::STREAM_TYPING) {
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
                                  std::string& accumulated_text,
                                  int& iterations) {
    if (tool_calls.empty()) {
        return false;
    }

    LOG_DEBUG("LLM requested {} tool calls", tool_calls.size());

    // Build assistant message with tool calls
    MessageSchema assistant_msg;
    assistant_msg.role = "assistant";
    assistant_msg.AddTextContent(accumulated_text);

    // Execute tools and build results message
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
    SetSessionOutput(session, AgentRuntimeState::TOOL_MSG, "", assistant_msg);
    SetSessionOutput(session, AgentRuntimeState::TOOL_MSG, "", results_msg);

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
        // 从 role 获取模型配置
        if (session.role) {
            req.model = session.role->model;
            req.temperature = session.role->temperature;
            req.max_tokens = 4096;
        } else {
            req.model = "claude-sonnet-4-6";
            req.temperature = 0.7;
            req.max_tokens = 4096;
        }
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

    // 从 role 获取模型配置
    if (session.role) {
        req.model = session.role->model;
        req.temperature = session.role->temperature;
        req.max_tokens = session.role->max_tokens;
    } else {
        // 降级使用默认值
        req.model = "claude-sonnet-4-6";
        req.temperature = 0.7;
        req.max_tokens = 8192;
    }

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
    SetSessionOutput(session, AgentRuntimeState::THINKING, "Processing...");

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
        try {
            ChatRequest request = BuildRequest(session);
            request.stream = streaming;

            // Call LLM - streaming or non-streaming
            ChatResponse response;
            if (streaming) {
                // Streaming mode: send STREAM_MODE_START first, then STREAM_TYPING for each chunk
                SetSessionOutput(session, AgentRuntimeState::STREAM_MODE_START, "");
                response = session.provider->ChatStream(
                    request, [&session](const ChatResponse& chunk) {
                        if (!chunk.content_text.empty()){
                            MessageSchema chunk_msg;
                            chunk_msg.role = "assistant";
                            chunk_msg.AddTextContent(chunk.content_text);
                            SetSessionOutput(session, AgentRuntimeState::STREAM_TYPING, "", chunk_msg);
                        }
                    });
            } else {
                response = session.provider->Chat(request);
            }

            // Execute tool calls - use response.tool_calls directly
            std::string response_text = response.content_text;
            if (ExecuteToolCalls(response.tool_calls, session, response_text, iterations)) {
                continue;  // Tool calls were executed, continue loop
            }

            // No tool calls - check for text response
            if (!response.content_text.empty()) {
                MessageSchema final_msg;
                final_msg.role = "assistant";
                final_msg.AddTextContent(response.content_text);

                // Set COMPLETE state
                if(streaming) {
                    SetSessionOutput(session, AgentRuntimeState::STREAM_MODE_COMPLETE, "Done.", final_msg);
                } else {
                    SetSessionOutput(session, AgentRuntimeState::COMPLETE, "Done.", final_msg);
                }
                return;
            }

            if (session.stop_requested) {
                MessageSchema stop_msg;
                stop_msg.role = "assistant";
                stop_msg.AddTextContent("[Agent turn stopped by user]");
                SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, "Stopped by user", stop_msg);
                return;
            }

            SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, "Unexpected LLM response format");
            break;

        } catch (const std::exception& e) {
            // Re-throw interrupt exceptions immediately
            std::string err = std::string(e.what());
            if (err.find("Interrupted by user") != std::string::npos) {
                SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, "Interrupted by user");
                throw;
            }

            // Don't retry Ollama timeout errors - they are already retried in OllamaProvider
            bool is_ollama_timeout = err.find("Ollama API error") != std::string::npos &&
                                     err.find("Timeout was reached") != std::string::npos;

            LOG_ERROR("Error in LLM processing: {}", e.what());
            if (!is_ollama_timeout && iterations < max_iterations - 1) {
                // Retry non-timeout errors (e.g., malformed response)
                std::this_thread::sleep_for(
                    std::chrono::seconds(1 << std::min(iterations, 4)));
                iterations++;
                continue;
            }
            // Set output: state + message
            SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, e.what());
            throw;
        }
    }

    // Max iterations or stopped
    std::string stop_text =
        session.stop_requested ? "[Stopped]" : "[Max iterations reached]";
    SetSessionOutput(session, AgentRuntimeState::STATE_ERROR, stop_text);
    throw std::runtime_error("Failed to get valid response after " +
                             std::to_string(max_iterations) + " iterations");
}

}  // namespace aicode
