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
#include "managers/memory_manager.h"
#include "core/skill_loader.h"
#include "core/agent_role.h"
#include "core/compact_service.h"
#include "managers/token_tracker.h"
#include "core/reference_parser.h"
#include "core/system_prompt.h"
#include "tools/tool_registry.h"
#include "services/lsp_manager.h"

namespace aicode {

// Event name constants
namespace events {
inline constexpr const char* kTextDelta = "text_delta";
inline constexpr const char* kToolUse = "tool_use";
inline constexpr const char* kToolResult = "tool_result";
inline constexpr const char* kMessageEnd = "message_end";
}  // namespace events

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

AgentCore::AgentCore(std::shared_ptr<MemoryManager> memory_manager,
                     std::shared_ptr<SkillLoader> skill_loader,
                     std::vector<ToolsSchema> tool_schemas,
                     std::function<std::string(const std::string& tool_name, const nlohmann::json& args)> tool_executor,
                     std::function<ChatResponse(const ChatRequest& request)> chat_llm_cb,
                     std::function<void(const ChatRequest&, std::function<void(const ChatResponse&)>)> chat_completion_stream,
                     const AgentConfig& agent_config)
    : memory_manager_(memory_manager),
      skill_loader_(skill_loader),
      tool_schemas_(std::move(tool_schemas)),
      tool_executor_(std::move(tool_executor)),
      chat_llm_cb_(std::move(chat_llm_cb)),
      chat_llm_stream_cb_(std::move(chat_completion_stream)),
      stop_requested_(false),
      max_iterations_(15) {
    max_iterations_ = agent_config_.DynamicMaxIterations();
    LOG_INFO("AgentCore initialized with model: {}, max_iterations: {}",
                  agent_config_.model, max_iterations_);

    // Add tool schemas only if enabled
    if (agent_config_.use_tools) {
        chat_request_.tools = tool_schemas_;
        chat_request_.tool_choice_auto = true;
    }

    SetConfig(agent_config);
}

void AgentCore::SetModel(const std::string& model_ref) {
    agent_config_.model = model_ref;
    chat_request_.model = model_ref;
    LOG_INFO("Model set to: {}", model_ref);
}

void AgentCore::SetSystemPrompt(const std::vector<SystemSchema>& system_prompt, bool is_cache) {
    chat_request_.system.clear();
    for (const auto& schema : system_prompt) {
        chat_request_.system.push_back(schema);
    }
    LOG_DEBUG("System prompt set with {} schemas (cache={})", system_prompt.size(), is_cache);
}

std::vector<MessageSchema> AgentCore::CloseLoop(const std::string& message) {
    LOG_DEBUG("Processing message (non-streaming)");
    stop_requested_ = false;

    std::vector<MessageSchema> res_messages;

    // Process message - resolve @file references
    std::string processed_message = message;
    if (!message.empty()) {
        // Check for file references
        if (ReferenceParser::HasFileRefs(message)) {
            auto workspace = memory_manager_ ? memory_manager_->GetWorkspace() : "";
            auto file_refs = ReferenceParser::ParseFileRefs(message, workspace);

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
        }
    }

    // Add user message (with resolved references)
    if (!processed_message.empty()) {
        chat_request_.messages.emplace_back("user", processed_message);
    }

    // Check if compaction is needed
    auto& compact_service = CompactService::GetInstance();
    if (compact_service.NeedsCompaction(chat_request_.messages)) {
        LOG_INFO("Context compaction triggered");

        auto llm_callback = [this](const std::string& prompt) -> std::string {
            ChatRequest req;
            req.model = agent_config_.model;
            req.temperature = agent_config_.temperature;
            req.max_tokens = 4096;
            req.AddUserMessage(prompt);
            auto resp = chat_llm_cb_(req);
            return resp.content_text;
        };

        auto compact_result = compact_service.Compact(chat_request_.messages, llm_callback);
        chat_request_.messages = compact_result.kept_messages;

        // Add summary to system prompt
        if (!compact_result.summary.empty()) {
            chat_request_.system.clear();
            chat_request_.system.push_back({"text", compact_result.summary, false});
        }

        LOG_INFO("Compaction complete: removed {} messages, saved ~{} tokens",
                 compact_result.messages_removed, compact_result.tokens_saved);
    }

    int iterations = 0;

    while (iterations < max_iterations_) {
        // Check for interrupt
        if (stop_requested_) {
            throw std::runtime_error("Interrupted by user");
        }

        try {
            // 1. 标准化：调用大模型的接口，定义入参，出参，接口
            // 2. chat_with_llm_单次调用，内部保持无状态
            // 3. 闭环放在CloseLoop里，不在chat_with_llm_里，状态量都在这一层
            auto response = chat_llm_cb_(chat_request_);

            if (!response.tool_calls.empty()) {
                LOG_INFO("LLM requested {} tool calls", response.tool_calls.size());

                // Build assistant message with tool calls
                MessageSchema assistant_msg;
                assistant_msg.role = "assistant";
                assistant_msg.AddTextContent(response.content_text);


                // Execute tools and build results message
                MessageSchema results_msg;
                results_msg.role = "user";
                bool has_tool_error = false;
                std::string tool_error_msg;

                for (const auto& tc : response.tool_calls) {
                    // Check for interrupt during tool execution
                    if (stop_requested_) {
                        throw std::runtime_error("Interrupted by user");
                    }

                    LOG_INFO("  Tool: {}, args: {}", tc.name, tc.arguments.dump());

                    // Add tool call to assistant message
                    assistant_msg.AddToolUseContent(tc.id, tc.name, tc.arguments);

                    try {
                        auto result = tool_executor_(tc.name, tc.arguments);
                        result = TruncateToolResult(result, kToolResultMaxChars, kToolResultKeepLines);
                        results_msg.AddToolResultContent(tc.id, result);
                    } catch (const std::exception& e) {
                        // Tool execution failed - add error result to inform LLM
                        has_tool_error = true;
                        tool_error_msg = e.what();
                        std::string error_result = "Error: " + tool_error_msg;
                        LOG_WARN("Tool {} execution failed: {}", tc.name, e.what());
                        results_msg.AddToolResultContent(tc.id, error_result);
                    }
                }

                // Update request history for next iteration
                chat_request_.messages.push_back(assistant_msg);
                chat_request_.messages.push_back(results_msg);

                res_messages.push_back(assistant_msg);
                res_messages.push_back(results_msg);

                iterations++;

                // If tool had errors, LLM will see the error and can decide what to do next
                if (has_tool_error) {
                    LOG_INFO("Tool execution had errors, continuing to let LLM handle");
                }
                continue;
            }

            if (!response.content_text.empty()) {
                LOG_INFO("LLM provided final response");
                MessageSchema final_msg;
                final_msg.role = "assistant";
                final_msg.AddTextContent(response.content_text);
                res_messages.push_back(final_msg);
                return res_messages;
            }

            if (stop_requested_) {
                MessageSchema stop_msg;
                stop_msg.role = "assistant";
                stop_msg.AddTextContent("[Agent turn stopped by user]");
                res_messages.push_back(stop_msg);
                return res_messages;
            }

            LOG_ERROR("Unexpected LLM response format");
            break;

        } catch (const std::exception& e) {
            // Re-throw interrupt exceptions immediately
            std::string err = std::string(e.what());
            if (err.find("Interrupted by user") != std::string::npos) {
                throw;
            }

            LOG_ERROR("Error in LLM processing: {}", e.what());
            if (iterations < max_iterations_ - 1) {
                std::this_thread::sleep_for(
                    std::chrono::seconds(1 << std::min(iterations, 4)));
                iterations++;
                continue;
            }
            throw;
        }
    }

    throw std::runtime_error("Failed to get valid response after " +
                             std::to_string(max_iterations_) + " iterations");
}

std::vector<MessageSchema> AgentCore::LoopStream(
    const std::string& message, AgentEventCallback callback) {
    LOG_DEBUG("Processing message (streaming)");
    stop_requested_ = false;

    std::vector<MessageSchema> new_memory;

    // Add user message
    if (!message.empty()) {
        chat_request_.messages.emplace_back("user", message);
    }

    // Check if compaction is needed
    auto& compact_service = CompactService::GetInstance();
    if (compact_service.NeedsCompaction(chat_request_.messages)) {
        LOG_INFO("Context compaction triggered (streaming)");

        auto llm_callback = [this](const std::string& prompt) -> std::string {
            ChatRequest req;
            req.model = agent_config_.model;
            req.temperature = agent_config_.temperature;
            req.max_tokens = 4096;
            req.AddUserMessage(prompt);
            auto resp = chat_llm_cb_(req);
            return resp.content_text;
        };

        auto compact_result = compact_service.Compact(chat_request_.messages, llm_callback);
        chat_request_.messages = compact_result.kept_messages;

        // Add summary to system prompt
        if (!compact_result.summary.empty()) {
            chat_request_.system.clear();
            chat_request_.system.push_back({"text", compact_result.summary, false});
        }

        LOG_INFO("Compaction complete: removed {} messages, saved ~{} tokens",
                 compact_result.messages_removed, compact_result.tokens_saved);
    }

    // Use member chat_request_ - set stream flag
    chat_request_.stream = true;

    int iterations = 0;

    while (iterations < max_iterations_ && !stop_requested_) {
        try {
            std::string full_response;

            chat_llm_stream_cb_(
                chat_request_, [&](const ChatResponse& chunk) {
                    if (!chunk.content_text.empty()) {
                        full_response += chunk.content_text;
                        if (callback) {
                            callback({events::kTextDelta, {{"text", chunk.content_text}}});
                        }
                    }

                    if (!chunk.tool_calls.empty()) {
                        for (const auto& tc : chunk.tool_calls) {
                            if (callback) {
                                callback({events::kToolUse,
                                        {{"id", tc.id},
                                         {"name", tc.name},
                                         {"input", tc.arguments}}});
                            }

                            // Construct assistant message
                            MessageSchema assistant_msg;
                            assistant_msg.role = "assistant";
                            if (!full_response.empty())
                                assistant_msg.AddTextContent(full_response);
                            assistant_msg.AddToolUseContent(tc.id, tc.name, tc.arguments);
                            chat_request_.messages.push_back(assistant_msg);
                            new_memory.push_back(assistant_msg);
                            full_response.clear();

                            // Execute tool
                            try {
                                auto result =
                                    tool_executor_(tc.name, tc.arguments);
                                result = TruncateToolResult(result, kToolResultMaxChars,
                                                            kToolResultKeepLines);
                                if (callback) {
                                    callback({events::kToolResult,
                                            {{"tool_use_id", tc.id}, {"content", result}}});
                                }

                                MessageSchema results_msg;
                                results_msg.role = "user";
                                results_msg.AddToolResultContent(tc.id, result);
                                chat_request_.messages.push_back(results_msg);
                                new_memory.push_back(results_msg);
                            } catch (const std::exception& e) {
                                std::string error_content = "Error: " + std::string(e.what());
                                if (callback) {
                                    callback({events::kToolResult,
                                            {{"tool_use_id", tc.id},
                                             {"content", error_content},
                                             {"is_error", true}}});
                                }

                                MessageSchema results_msg;
                                results_msg.role = "user";
                                results_msg.AddToolResultContent(tc.id, error_content);
                                chat_request_.messages.push_back(results_msg);
                                new_memory.push_back(results_msg);
                            }
                        }
                        iterations++;
                        return;
                    }

                    if (chunk.is_stream_end) {
                        if (callback) {
                            callback({events::kMessageEnd, {{"content_text", full_response}}});
                        }
                    }
                });

            if (!full_response.empty()) {
                MessageSchema final_msg;
                final_msg.role = "assistant";
                final_msg.AddTextContent(full_response);
                new_memory.push_back(final_msg);
                return new_memory;
            }

            iterations++;

        } catch (const std::exception& e) {
            LOG_ERROR("Error in streaming: {}", e.what());
            if (callback) {
                callback({events::kMessageEnd, {{"message", e.what()}}});
            }
            return new_memory;
        }
    }

    std::string stop_text =
        stop_requested_ ? "[Stopped]" : "[Max iterations reached]";
    if (callback) {
        callback({events::kMessageEnd, {{"message", stop_text}}});
    }
    MessageSchema stop_msg;
    stop_msg.role = "assistant";
    stop_msg.AddTextContent(stop_text);
    new_memory.push_back(stop_msg);
    return new_memory;
}

void AgentCore::Stop() {
    stop_requested_ = true;
    LOG_INFO("Agent stop requested");
}

void AgentCore::SetConfig(const AgentConfig& config) {
    agent_config_ = config;
    chat_request_.model = config.model;
    chat_request_.temperature = config.temperature;
    chat_request_.max_tokens = config.max_tokens;
    chat_request_.thinking = config.thinking;
    max_iterations_ = config.DynamicMaxIterations();
    LOG_INFO(
        "AgentCore config updated: model={}, temp={}, max_tokens={}, "
        "max_iterations={}, thinking={}",
        config.model, config.temperature, config.max_tokens, max_iterations_,
        config.thinking);
}

void AgentCore::InitializeLsp() {
    auto& lsp_manager = aicode::LspManager::GetInstance();
    lsp_manager.Initialize();
    LOG_INFO("LSP integration initialized with {} servers",
             lsp_manager.GetRegisteredServers().size());
}

bool AgentCore::RequestLspForFile(const std::string& filepath) {
    auto& lsp_manager = aicode::LspManager::GetInstance();
    return lsp_manager.StartServerForFile(filepath);
}

}  // namespace aicode
