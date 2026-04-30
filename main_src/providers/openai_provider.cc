// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/openai_provider.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"

#include "common/curl_client.h"
#include "providers/llm_provider.h"
#include "common/messages_schema.h"

namespace prosophor {

// Internal helper functions for OpenAI serialization

// Maps thinking level to enable_thinking flag
static bool ShouldEnableThinking(const std::string& level) {
    return level != "off" && !level.empty();
}

// Maps thinking level to budget tokens (OpenAI uses reasoning_effort style)
static std::string ThinkingToReasoningEffort(const std::string& level) {
    if (level == "low") return "low";
    if (level == "medium") return "medium";
    if (level == "high") return "high";
    return "";
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
            if (i > 0) system_text += "\n";
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
            if (!text.empty()) text += "\n";
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
                        if (block.value("type", "") == "text") {
                            if (!result.content_text.empty()) result.content_text += "\n";
                            result.content_text += block.value("text", "");
                        }
                    }
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

// OpenAI-compatible SSE stream handler
// Format (matching openai_demo.py):
//   data: {"choices":[{"delta":{"reasoning_content":"...","content":"..."}}]}
//   data: [DONE]
struct OpenAIStreamHandler : public StreamHandler {
    std::function<void(const ChatResponse&)> chat_response_callback;
    std::string buffer;

    struct PendingToolCall {
        std::string id;
        std::string name;
        std::string arguments;
    };
    std::vector<PendingToolCall> pending_tool_calls;

    // Accumulated response for return value
    ChatResponse accumulated_response;

    explicit OpenAIStreamHandler(std::function<void(const ChatResponse&)> cb)
        : chat_response_callback(std::move(cb)) {}

    std::string& Buffer() override { return buffer; }

    void OnLine(const std::string& line) override {
        if (line.empty()) return;

        // Handle SSE "data:" prefix
        std::string data = line;
        if (data.rfind("data:", 0) == 0) {
            data = data.substr(5);
            // Trim whitespace
            size_t start = data.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                data = data.substr(start);
            } else {
                return;
            }
        }

        // Check for [DONE] signal
        if (data == "[DONE]" || data.empty()) {
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
            end_resp.stop_reason = accumulated_response.stop_reason.empty()
                ? "stop" : accumulated_response.stop_reason;
            end_resp.usage = accumulated_response.usage;
            chat_response_callback(end_resp);
            return;
        }

        try {
            auto chunk = nlohmann::json::parse(data, nullptr, false);
            if (chunk.is_discarded()) return;

            if (!chunk.contains("choices") || chunk["choices"].empty()) {
                return;
            }

            const auto& delta = chunk["choices"][0].value("delta", nlohmann::json::object());
            std::string finish = chunk["choices"][0].value("finish_reason", "");

            // Parse reasoning_content (Qwen DeepThink style)
            if (delta.contains("reasoning_content")
                && !delta["reasoning_content"].is_null()) {
                std::string rc = delta["reasoning_content"].get<std::string>();
                if (!rc.empty()) {
                    ChatResponse resp;
                    resp.content_text = rc;
                    // Mark as reasoning content via a special prefix
                    accumulated_response.content_text += rc;
                    chat_response_callback(resp);
                }
            }

            // Parse regular content
            if (delta.contains("content")
                && !delta["content"].is_null()) {
                std::string cc = delta["content"].get<std::string>();
                if (!cc.empty()) {
                    ChatResponse resp;
                    resp.content_text = cc;
                    accumulated_response.content_text += cc;
                    chat_response_callback(resp);
                }
            }

            // Parse tool calls in streaming
            if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                for (const auto& tc : delta["tool_calls"]) {
                    if (tc.contains("function")) {
                        const auto& func = tc["function"];

                        // Initialize pending tool call if new
                        if (tc.contains("id") && !tc["id"].get<std::string>().empty()) {
                            PendingToolCall ptc;
                            ptc.id = tc["id"].get<std::string>();
                            if (func.contains("name")) {
                                ptc.name = func["name"].get<std::string>();
                            }
                            pending_tool_calls.push_back(ptc);
                        }

                        // Append arguments
                        if (!pending_tool_calls.empty()
                            && func.contains("arguments")) {
                            std::string args_str = func["arguments"].get<std::string>();
                            if (!args_str.empty()) {
                                pending_tool_calls.back().arguments += args_str;
                            }
                        }
                    }
                }
            }

            // Handle finish_reason
            if (!finish.empty()) {
                accumulated_response.stop_reason = finish;
                if (finish == "stop") {
                    accumulated_response.stop_reason = "stop";
                } else if (finish == "length") {
                    accumulated_response.stop_reason = "length";
                } else if (finish == "tool_calls") {
                    accumulated_response.stop_reason = "tool_calls";
                }
            }

            // Parse usage from streaming chunk
            if (chunk.contains("usage")) {
                const auto& u = chunk["usage"];
                accumulated_response.usage.prompt_tokens = u.value("prompt_tokens", 0);
                accumulated_response.usage.completion_tokens = u.value("completion_tokens", 0);
                accumulated_response.usage.total_tokens = u.value("total_tokens", 0);
            }

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse OpenAI stream chunk: {}", e.what());
        }
    }
};

ChatResponse OpenAIProvider::ChatStream(const ChatRequest& request,
     std::function<void(const ChatResponse&)> callback) {
    OpenAIStreamHandler stream_handler(std::move(callback));
    PrintRequestLog(request);
    ExecuteStream(request, &stream_handler);
    return stream_handler.accumulated_response;
}

}  // namespace prosophor
