// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/openai_provider.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"

#include "common/curl_client.h"
#include "providers/llm_provider.h"
#include "common/messages_schema.h"
#include "providers/detail/openai_stream_handler.h"

namespace prosophor {

// Internal helper functions for OpenAI serialization

// Maps thinking level to enable_thinking flag
static bool ShouldEnableThinking(bool thinking) {
    return thinking;
}

// Maps thinking level to budget tokens (OpenAI uses reasoning_effort style)
static std::string ThinkingToReasoningEffort(bool /*thinking*/) {
    return "medium";
}

OpenAIProvider::OpenAIProvider(bool enable_thinking)
    : enable_thinking_(enable_thinking) {
    LOG_DEBUG("OpenAIProvider initialized");
}

/**
 * Serialize ChatRequest to OpenAI-compatible API JSON payload
 *
 * Request format (matching openai_demo.py):
 * {
 *   "model": string,
 *   "max_tokens": number,
 *   "messages": [
 *     {"role": "system", "content": "..."},     // optional, first
 *     {"role": "user", "content": "..."},
 *     {"role": "assistant", "content": "..."},
 *     {"role": "tool", "content": "..."}
 *   ],
 *   "stream": boolean,
 *   "enable_thinking": boolean,                  // optional, for reasoning models
 *   "tools": [                                   // optional
 *     {
 *       "type": "function",
 *       "function": {
 *         "name": string,
 *         "description": string,
 *         "parameters": object
 *       }
 *     }
 *   ],
 *   "tool_choice": "auto" | {...}
 * }
 */
std::string OpenAIProvider::Serialize(const ChatRequest& request) const {
    nlohmann::json payload_json;
    payload_json["model"] = request.model;
    payload_json["max_tokens"] = request.max_tokens;

    // Build messages array
    nlohmann::json messages_json = nlohmann::json::array();

    // Add system message as first entry (OpenAI style - system in messages[])
    if (!request.system.empty()) {
        nlohmann::json sys_msg;
        sys_msg["role"] = "system";
        // Join system texts into a single string
        std::string system_text;
        for (size_t i = 0; i < request.system.size(); ++i) {
            if (i > 0) {
                system_text += "\n";
            }
            system_text += request.system[i].text;
        }
        sys_msg["content"] = system_text;
        messages_json.push_back(sys_msg);
    }

    // Add conversation messages
    for (const auto& msg : request.messages) {
        nlohmann::json msg_obj;
        msg_obj["role"] = msg.role;
        msg_obj["content"] = SerializeMessageContent(msg.content);
        messages_json.push_back(msg_obj);
    }

    payload_json["messages"] = messages_json;

    if (request.stream) {
        payload_json["stream"] = true;
    }

    // Enable thinking if provider supports it OR request asks for it
    bool think = enable_thinking_ || ShouldEnableThinking(request.thinking);
    if (think) {
        payload_json["enable_thinking"] = true;
        std::string effort = ThinkingToReasoningEffort(request.thinking);
        if (!effort.empty()) {
            payload_json["reasoning_effort"] = effort;
        }
    }
    else {
        payload_json["enable_thinking"] = false;
    }

    // Serialize tools (OpenAI format: {type: "function", function: {...}})
    if (!request.tools.empty()) {
        payload_json["tools"] = SerializeTools(request.tools);
        if (request.tool_choice_auto) {
            payload_json["tool_choice"] = "auto";
        }
    }

    return payload_json.dump();
}

nlohmann::json OpenAIProvider::SerializeMessageContent(
    const std::vector<ContentSchema>& content) const {
    // Check for tool-related content
    bool has_tool_content = false;
    for (const auto& block : content) {
        if (block.type == "tool_use" || block.type == "tool_result") {
            has_tool_content = true;
            break;
        }
    }

    if (has_tool_content) {
        // Use array format for tool messages
        nlohmann::json content_arr = nlohmann::json::array();
        for (const auto& block : content) {
            if (block.type == "text" || block.type == "thinking") {
                if (!block.text.empty()) {
                    nlohmann::json block_json;
                    block_json["type"] = "text";
                    block_json["text"] = block.text;
                    content_arr.push_back(block_json);
                }
            } else if (block.type == "tool_result") {
                nlohmann::json block_json;
                block_json["type"] = "text";
                block_json["text"] = block.content;
                content_arr.push_back(block_json);
            }
        }
        return content_arr;
    }

    // Simple text - join content blocks
    std::string text;
    for (const auto& block : content) {
        if (block.type == "text" || block.type == "thinking") {
            if (!text.empty()) {
                text += "\n";
            }
            text += block.text;
        }
    }
    return text;
}

nlohmann::json OpenAIProvider::SerializeTools(
    const std::vector<ToolsSchema>& tools) const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& schema : tools) {
        nlohmann::json tool_json;
        tool_json["type"] = "function";

        nlohmann::json func_json;
        func_json["name"] = schema.name;
        func_json["description"] = schema.description;

        if (!schema.input_schema.is_null()) {
            func_json["parameters"] = schema.input_schema;
        } else {
            func_json["parameters"] = nlohmann::json::object();
            func_json["parameters"]["type"] = "object";
            func_json["parameters"]["properties"] = nlohmann::json::object();
        }

        tool_json["function"] = func_json;
        arr.push_back(tool_json);
    }
    return arr;
}

/**
 * Deserialize OpenAI-compatible API response JSON to ChatResponse
 *
 * Response format:
 * {
 *   "id": string,
 *   "object": "chat.completion",
 *   "model": string,
 *   "choices": [{
 *     "index": 0,
 *     "message": {
 *       "role": "assistant",
 *       "content": string | null,
 *       "reasoning_content": string  // optional
 *     },
 *     "finish_reason": "stop" | "length" | "tool_calls"
 *   }],
 *   "usage": {
 *     "prompt_tokens": number,
 *     "completion_tokens": number,
 *     "total_tokens": number
 *   }
 * }
 */
ChatResponse OpenAIProvider::Deserialize(const std::string& json_str) const {
    nlohmann::json response_json;
    try {
        response_json = nlohmann::json::parse(json_str);
    } catch (...) {
        LOG_INFO("Response (raw):\n{}", json_str);
        throw;
    }

    ChatResponse result;

    // Parse choices[0].message
    if (response_json.contains("choices") && response_json["choices"].is_array()
        && !response_json["choices"].empty()) {
        const auto& choice = response_json["choices"][0];

        if (choice.contains("message")) {
            const auto& msg = choice["message"];

            // Parse content
            if (msg.contains("content") && !msg["content"].is_null()) {
                if (msg["content"].is_string()) {
                    result.content_text = msg["content"].get<std::string>();
                } else if (msg["content"].is_array()) {
                    // Array format content
                    for (const auto& block : msg["content"]) {
                        std::string btype = block.value("type", "");
                        if (btype == "text") {
                            if (!result.content_text.empty()) {
                                result.content_text += "\n";
                            }
                            result.content_text += block.value("text", "");
                        } else if (btype == "thinking") {
                            result.has_thinking = true;
                        }
                    }
                }
            }

            // Parse reasoning_content (DeepSeek, Qwen, etc.)
            if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null()) {
                result.has_thinking = true;
                std::string rc = msg["reasoning_content"].get<std::string>();
                if (!rc.empty()) {
                    result.AddThinking(rc);
                }
            }

            // Parse tool calls
            if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                for (const auto& tc : msg["tool_calls"]) {
                    if (tc.contains("function")) {
                        const auto& func = tc["function"];
                        std::string name = func.value("name", "");
                        std::string id = tc.value("id", "");

                        nlohmann::json args;
                        if (func.contains("arguments")) {
                            if (func["arguments"].is_object()) {
                                args = func["arguments"];
                            } else if (func["arguments"].is_string()) {
                                args = nlohmann::json::parse(
                                    func["arguments"].get<std::string>(),
                                    nullptr, false);
                                if (args.is_discarded()) {
                                    args = nlohmann::json::object();
                                }
                            }
                        }

                        result.AddToolCall(id, name, args);
                    }
                }
            }
        }

        // Parse finish_reason
        std::string finish_reason = choice.value("finish_reason", "");
        if (finish_reason == "stop") {
            result.stop_reason = "stop";
        } else if (finish_reason == "length") {
            result.stop_reason = "length";
        } else if (finish_reason == "tool_calls") {
            result.stop_reason = "tool_calls";
        } else {
            result.stop_reason = finish_reason;
        }
    }

    // Parse usage
    if (response_json.contains("usage")) {
        const auto& u = response_json["usage"];
        result.usage.prompt_tokens = u.value("prompt_tokens", 0);
        result.usage.completion_tokens = u.value("completion_tokens", 0);
        result.usage.total_tokens = u.value("total_tokens", 0);
    }

    return result;
}

void OpenAIProvider::PrintRequestLog(const ChatRequest& request) const {
    LOG_INFO("=== Sending request to OpenAI-compatible API ===");
    LOG_INFO("URL: {}", request.base_url);
    LOG_INFO("Model: {}", request.model);
    LOG_INFO("Headers:");
    LOG_INFO("  Content-Type: application/json");
    LOG_INFO("  Authorization: Bearer {}", request.api_key.substr(0, 8) + "...");
}

HeaderList OpenAIProvider::CreateHeaders(const ChatRequest& request) const {
    HeaderList headers;
    headers.append("Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer " + request.api_key;
    headers.append(auth_header.c_str());
    return headers;
}

// --- SSE Streaming support ---

ChatResponse OpenAIProvider::ChatStream(const ChatRequest& request,
     std::function<void(StreamEvent, std::string)> callback) {
    OpenAIStreamHandler stream_handler(std::move(callback));
    PrintRequestLog(request);
    auto http_resp = ExecuteStream(request, &stream_handler);
    if (!stream_handler.error_msg.empty()) {
        stream_handler.accumulated_response.error_msg = stream_handler.error_msg;
        stream_handler.stream_callback(StreamEvent::kError, stream_handler.error_msg);
    } else if (http_resp.failed()) {
        std::string err = "OpenAI API error (HTTP " +
                          std::to_string(http_resp.status_code) + "): " + http_resp.error_msg;
        LOG_ERROR("{}", err);
        stream_handler.accumulated_response.error_msg = err;
        stream_handler.stream_callback(StreamEvent::kError, std::move(err));
    }
    return stream_handler.accumulated_response;
}

}  // namespace prosophor
