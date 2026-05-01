// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/anthropic_provider.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"
#include "common/string_utils.h"

#include "common/curl_client.h"
#include "providers/llm_provider.h"
#include "common/messages_schema.h"

namespace prosophor {

// Internal helper functions for Anthropic serialization (static to avoid linkage conflicts)

// Maps thinking level to budget tokens
static int ThinkingBudgetTokens(const std::string& level) {
    if (level == "low") return 1024;
    if (level == "medium") return 4096;
    if (level == "high") return 16000;
    return 0;
}

// Applies thinking parameters to payload_json
static void ApplyThinkingParams(nlohmann::json& payload_json,
     const ChatRequest& request) {
    int budget = ThinkingBudgetTokens(request.thinking);
    if (budget > 0) {
        payload_json["thinking"] = nlohmann::json::object();
        payload_json["thinking"]["type"] = "enabled";
        payload_json["thinking"]["budget_tokens"] = budget;
        payload_json["temperature"] = 1;
        if (request.max_tokens <= budget) {
            payload_json["max_tokens"] = budget + 4096;
        }
    }
}

// Serialize message content to Anthropic format
static nlohmann::json SerializeMessageContent(const std::vector<ContentSchema>& content) {
    // Check if message has tool-related content
    bool has_tool_content = false;
    bool has_thinking = false;
    for (const auto& block : content) {
        if (block.type == "tool_use" || block.type == "tool_result") {
            has_tool_content = true;
        }
        if (block.type == "thinking" && !block.text.empty()) {
            has_thinking = true;
        }
    }

    if (has_tool_content || has_thinking) {
        // Use array format for tool messages or thinking blocks
        nlohmann::json content_arr = nlohmann::json::array();
        for (const auto& block : content) {
            if (block.type == "thinking") {
                if (!block.text.empty()) {
                    nlohmann::json block_json;
                    block_json["type"] = "thinking";
                    block_json["thinking"] = block.text;
                    content_arr.push_back(block_json);
                }
            } else if (block.type == "text") {
                if (!block.text.empty()) {
                    nlohmann::json block_json;
                    block_json["type"] = "text";
                    block_json["text"] = block.text;
                    content_arr.push_back(block_json);
                }
            } else if (block.type == "tool_use") {
                nlohmann::json block_json;
                block_json["type"] = "tool_use";
                block_json["id"] = block.tool_use_id;
                block_json["name"] = block.name;
                block_json["input"] = block.input;
                content_arr.push_back(block_json);
            } else if (block.type == "tool_result") {
                nlohmann::json block_json;
                block_json["type"] = "tool_result";
                block_json["tool_use_id"] = block.tool_use_id;
                block_json["content"] = block.content;
                content_arr.push_back(block_json);
            }
        }
        return content_arr;
    } else {
        // Use string format for simple text messages
        std::string text;
        for (const auto& block : content) {
            if (block.type == "text") {
                if (!text.empty()) text += "\n";
                text += block.text;
            }
        }
        return text;
    }
}

// Serialize tools to Anthropic format
static nlohmann::json SerializeTools(const std::vector<ToolsSchema>& tools) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& schema : tools) {
        nlohmann::json tool_json = nlohmann::json::object();
        tool_json["name"] = schema.name;
        tool_json["description"] = schema.description;
        if (!schema.input_schema.is_null()) {
            tool_json["input_schema"] = schema.input_schema;
        } else {
            tool_json["input_schema"] = nlohmann::json::object();
            tool_json["input_schema"]["type"] = "object";
            tool_json["input_schema"]["properties"] = nlohmann::json::object();
        }
        arr.push_back(tool_json);
    }
    return arr;
}

// --- Provider implementation ---


/**
 * Serialize ChatRequest to Anthropic API compatible JSON payload
 *
 * Request JSON Schema (Anthropic Messages API format):
 * {
 *   "model": string,           // 模型名称，如 "claude-3-5-sonnet-20241022"
 *   "temperature": number,     // 温度，0-2 之间
 *   "max_tokens": number,      // 最大生成 token 数
 *   "messages": [              // 消息数组
 *     {
 *       "role": "user" | "assistant",
 *       "content": string | [  // 文本或内容数组（有 tool 内容时用数组）
 *         {
 *           "type": "text" | "tool_use" | "tool_result",
 *           "text": string,           // text 类型时
 *           "id": string,             // tool_use 类型时
 *           "name": string,           // tool_use 类型时
 *           "input": object,          // tool_use 类型时
 *           "tool_use_id": string,    // tool_result 类型时
 *           "content": string         // tool_result 类型时
 *         }
 *       ]
 *     }
 *   ],
 *   "system": [                // 系统提示词数组（支持缓存控制）
 *     {
 *       "type": "text",
 *       "text": string,
 *       "cache_control": {      // 可选，用于 prompt 缓存
 *         "type": "global"
 *       }
 *     }
 *   ],
 *   "stream": boolean,         // 可选，是否流式输出
 *   "tools": [                 // 可选，工具定义
 *     {
 *       "name": string,
 *       "description": string,
 *       "input_schema": object   // JSON Schema
 *     }
 *   ],
 *   "tool_choice": {           // 可选，工具选择
 *     "type": "auto" | "any" | "tool"
 *   },
 *   "thinking": {              // 可选，思考参数（Claude 3.7+）
 *     "type": "enabled",
 *     "budget_tokens": number
 *   }
 * }
 */
std::string AnthropicProvider::Serialize(const ChatRequest& request) const {
    nlohmann::json payload_json;
    payload_json["model"] = request.model;
    payload_json["temperature"] = request.temperature;
    payload_json["max_tokens"] = request.max_tokens;

    // Serialize system messages as a single string (DashScope Anthropic-compatible format)
    if (!request.system.empty()) {
        std::string system_text;
        for (size_t i = 0; i < request.system.size(); ++i) {
            if (i > 0) system_text += "\n";
            system_text += request.system[i].text;
        }
        payload_json["system"] = system_text;
    }
    // if (!request.system.empty()) {
    //     nlohmann::json system_json = nlohmann::json::array();
    //     for (const auto& sys_msg : request.system) {
    //         nlohmann::json block;
    //         block["type"] = "text";
    //         block["text"] = sys_msg.text;
    //         if (sys_msg.cache_control) {
    //             block["cache_control"] = {{"type", "global"}};
    //         }
    //         system_json.push_back(block);
    //     }
    //     payload_json["system"] = system_json;
    // }

    // Serialize messages (excluding system role - those go in system field)
    nlohmann::json messages_json = nlohmann::json::array();
    for (const auto& msg : request.messages) {
        // Skip system messages - they're handled separately
        if (msg.role == "system") {
            continue;
        }

        std::string role = msg.role;
        if (role == "tool") role = "user";

        nlohmann::json msg_obj;
        msg_obj["role"] = role;
        msg_obj["content"] = SerializeMessageContent(msg.content);
        messages_json.push_back(msg_obj);
    }
    payload_json["messages"] = messages_json;

    if (request.stream) {
        payload_json["stream"] = true;
    }

    // Serialize tools and tool_choice
    if (!request.tools.empty()) {
        payload_json["tools"] = SerializeTools(request.tools);
        if (request.tool_choice_auto) {
            payload_json["tool_choice"] = {{"type", "auto"}};
        }
    }

    ApplyThinkingParams(payload_json, request);

    LOG_DEBUG("Request body:\n {}", payload_json.dump(4));

    return payload_json.dump();
}

/**
 * Deserialize Anthropic API response JSON to ChatResponse
 *
 * Response JSON Schema (Anthropic Messages API format):
 * {
 *   "id": string,              // 请求 ID
 *   "type": "message",
 *   "role": "assistant",
 *   "content": [               // 内容块数组
 *     {
 *       "type": "text" | "tool_use",
 *       "text": string,        // text 类型时
 *       "id": string,          // tool_use 类型时
 *       "name": string,        // tool_use 类型时
 *       "input": object        // tool_use 类型时
 *     }
 *   ],
 *   "stop_reason": "end_turn" | "tool_use" | "max_tokens",
 *   "usage": {                 // Token 使用统计
 *     "input_tokens": number,
 *     "output_tokens": number
 *   }
 * }
 *
 * 流式响应 (SSE):
 * event: content_block_start
 * data: {"content_block": {"type": "text" | "tool_use", ...}}
 *
 * event: content_block_delta
 * data: {"delta": {"type": "text_delta", "text": "..."}}
 * data: {"delta": {"type": "input_json_delta", "partial_json": "..."}}
 *
 * event: message_delta
 * data: {"stop_reason": "end_turn" | "tool_use"}
 *
 * event: message_stop
 * data: {}
 */
ChatResponse AnthropicProvider::Deserialize(const std::string& json_str) const {
    nlohmann::json response_json;

    try {
	#ifdef _WIN32
        // Convert to UTF-8 if necessary
        std::string utf8_json = ConvertToUtf8(json_str);
        response_json = nlohmann::json::parse(utf8_json);
	#else
        response_json = nlohmann::json::parse(json_str);	
	#endif
	
        LOG_DEBUG(" response body: {}", response_json.dump(4));
    } catch (...) {
        LOG_INFO("  response (raw):\n{}", json_str);
        throw;
    }

    ChatResponse result;

    // Parse content blocks
    if (response_json.contains("content") && response_json["content"].is_array()) {
        for (const auto& block : response_json["content"]) {
            std::string block_type = block.value("type", "");
            if (block_type == "thinking") {
                result.AddThinking(block.value("thinking", ""));
            } else if (block_type == "text") {
                result.AddText(block.value("text", ""));
            } else if (block_type == "tool_use") {
                result.AddToolCall(block.value("id", ""),
                                   block.value("name", ""),
                                   block.value("input", nlohmann::json::object()));
            }
        }
    }

    // tool use end turn
    result.stop_reason = response_json.value("stop_reason", "");

    // Parse usage
    if (response_json.contains("usage")) {
        auto& usage = response_json["usage"];
        result.usage.prompt_tokens = usage.value("input_tokens", 0);
        result.usage.completion_tokens = usage.value("output_tokens", 0);
        result.usage.total_tokens =
            result.usage.prompt_tokens + result.usage.completion_tokens;
    }

    return result;
}

// --- Provider-specific implementations ---

void AnthropicProvider::PrintRequestLog(const ChatRequest& request) const {
    LOG_DEBUG("=== Sending request to Anthropic API ===");
    LOG_DEBUG("URL: {}", request.base_url);
    LOG_DEBUG("Model: {}", request.model);
    LOG_DEBUG("Max tokens: {}", request.max_tokens);
    LOG_DEBUG("Messages count: {}", request.messages.size());
    LOG_DEBUG("System blocks: {}", request.system.size());
    LOG_DEBUG("Tools count: {}", request.tools.size());
    LOG_DEBUG("Streaming: {}", request.stream);
    LOG_DEBUG("Thinking: {}", request.thinking);
    LOG_DEBUG("Headers:");
    LOG_DEBUG("  Content-Type: application/json");
    LOG_DEBUG("  x-api-key: {}", request.api_key.substr(0, 8) + "...");
    LOG_DEBUG("  anthropic-version: 2023-06-01");
}

HeaderList AnthropicProvider::CreateHeaders(const ChatRequest& request) const {
    HeaderList headers;
    headers.append("Content-Type: application/json");
    std::string api_key_header = "x-api-key: " + request.api_key;
    headers.append(api_key_header.c_str());
    headers.append("anthropic-version: 2023-06-01");
    return headers;
}

// --- SSE Streaming support ---

// Anthropic SSE stream handler - pushes response to callback via external worker
struct AnthropicStreamHandler : public SseStreamHandler {
    std::function<void(const ChatResponse&)> chat_response_callback;
    std::string stop_reason;
    TokenUsageSchema usage;

    struct PendingToolCall {
        std::string id;
        std::string name;
        std::string arguments;
    };
    std::vector<PendingToolCall> pending_tool_calls;
    int current_block_index = -1;
    std::string current_block_type;
    std::string current_thinking;
    bool thinking_started = false;

    // Accumulated response for return value
    ChatResponse accumulated_response;
    bool thinking_active = false;

    explicit AnthropicStreamHandler(std::function<void(const ChatResponse&)> cb)
        : chat_response_callback(std::move(cb)) {}

    void OnEvent(const std::string& event_type, const std::string& data) {
        auto j = nlohmann::json::parse(data, nullptr, false);
        if (j.is_discarded()) return;

        if (event_type == "content_block_start") {
            current_block_index++;
            if (j.contains("content_block")) {
                const auto& block = j["content_block"];
                current_block_type = block.value("type", "");
                if (current_block_type == "thinking") {
                    thinking_started = true;
                    thinking_active = true;
                    current_thinking.clear();
                    ChatResponse resp;
                    resp.thinking_phase = "start";
                    chat_response_callback(resp);
                } else if (current_block_type == "text") {
                    ChatResponse resp;
                    resp.content_phase = "start";
                    chat_response_callback(resp);
                } else if (current_block_type == "tool_use") {
                    //LOG_DEBUG("response tool_use name: {}", block.dump());
                    PendingToolCall ptc;
                    ptc.id = block.value("id", "");
                    ptc.name = block.value("name", "");
                    pending_tool_calls.push_back(ptc);
                }
            }
        } else if (event_type == "content_block_stop") {
            if (current_block_type == "thinking" && thinking_active) {
                thinking_active = false;
                ChatResponse resp;
                resp.thinking_phase = "end";
                chat_response_callback(resp);
            } else if (current_block_type == "text") {
                ChatResponse resp;
                resp.content_phase = "end";
                chat_response_callback(resp);
            }
            current_block_type.clear();
        } else if (event_type == "content_block_delta") {
            if (j.contains("delta")) {
                const auto& delta = j["delta"];
                std::string delta_type = delta.value("type", "");

                if (delta_type == "thinking_delta") {
                    std::string text = delta.value("thinking", "");
                    if (!text.empty()) {
                        current_thinking += text;
                        ChatResponse resp;
                        resp.thinking_phase = "delta";
                        resp.content_thinking = std::move(text);
                        chat_response_callback(resp);
                    }
                } else if (delta_type == "text_delta") {
                    std::string text = delta.value("text", "");
                    if (!text.empty()) {
                        ChatResponse resp;
                        resp.content_phase = "delta";
                        resp.content_text = std::move(text);
                        accumulated_response.content_text += resp.content_text;
                        chat_response_callback(resp);
                    }
                } else if (delta_type == "input_json_delta") {
                    if (!pending_tool_calls.empty()) {                        
                        //LOG_DEBUG("response tool_use input: {}", delta.dump());
                        pending_tool_calls.back().arguments +=
                            delta.value("partial_json", "");
                    }
                }
            }
        } else if (event_type == "message_delta") {
            if (j.contains("delta") && j["delta"].contains("stop_reason")) {
                stop_reason = j["delta"]["stop_reason"].get<std::string>();
                accumulated_response.stop_reason = stop_reason;
            }
            if (j.contains("usage")) {
                const auto& u = j["usage"];
                usage.prompt_tokens = u.value("input_tokens", 0);
                usage.completion_tokens = u.value("output_tokens", 0);
                usage.total_tokens = usage.prompt_tokens + usage.completion_tokens;
                accumulated_response.usage = usage;
            }
        } else if (event_type == "message_stop") {
            // Add accumulated thinking blocks
            if (thinking_started && !current_thinking.empty()) {
                accumulated_response.AddThinking(current_thinking);
                thinking_started = false;
                current_thinking.clear();
            }

            // Finalize any pending tool calls
            if (!pending_tool_calls.empty()) {
                for (const auto& ptc : pending_tool_calls) {
                    auto args = nlohmann::json::parse(ptc.arguments, nullptr, false);
                    if (args.is_discarded()) {
                        args = nlohmann::json::object();
                    }
                    accumulated_response.AddToolCall(ptc.id, ptc.name, args);
                }
                pending_tool_calls.clear();
            }

            // Send end-of-stream notification
            ChatResponse end_resp;
            end_resp.is_stream_end = true;
            end_resp.stop_reason = stop_reason.empty() ? "end_turn" : stop_reason;
            end_resp.usage = usage;
            chat_response_callback(end_resp);
        }
    }
};

ChatResponse AnthropicProvider::ChatStream(const ChatRequest& request, std::function<void(const ChatResponse&)> callback) {
    AnthropicStreamHandler stream_handler(std::move(callback));
    PrintRequestLog(request);
    ExecuteStream(request, &stream_handler);
    return stream_handler.accumulated_response;
}

}  // namespace prosophor
