// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/qwen_provider.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"
#include "common/string_utils.h"

#include "common/curl_client.h"
#include "providers/llm_provider.h"
#include "common/messages_schema.h"

namespace aicode {

// Internal helper functions for Qwen serialization (static to avoid linkage conflicts)

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

// Serialize message content to Qwen format (same as Anthropic)
static nlohmann::json SerializeMessageContent(const std::vector<ContentSchema>& content) {
    // Check if message has tool-related content
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
            if (block.type == "text" || block.type == "thinking") {
                if (!text.empty()) text += "\n";
                text += block.text;
            }
        }
        return text;
    }
}

// Serialize tools to Qwen format (same as Anthropic)
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

QwenProvider::QwenProvider(const std::string& api_key,
     const std::string& base_url, int timeout)
    : api_key_(api_key),
      base_url_(base_url),
      timeout_(timeout) {
    if (base_url_.empty()) {
        base_url_ = "https://dashscope.aliyuncs.com";
    }

    LOG_INFO("QwenProvider initialized with base_url: {}",
             base_url_);
}

std::string QwenProvider::Serialize(const ChatRequest& request) const {
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

ChatResponse QwenProvider::Deserialize(const std::string& json_str) const {
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
            if (block_type == "text") {
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

void QwenProvider::PrintRequestLog(const std::string& url) const {
    LOG_DEBUG("=== Sending request to Qwen API ===");
    LOG_DEBUG("URL: {}", url);
    LOG_DEBUG("Headers:");
    LOG_DEBUG("  Content-Type: application/json");
    LOG_DEBUG("  Authorization: Bearer {}", api_key_.substr(0, 8) + "...");
    LOG_DEBUG("  anthropic-version: 2023-06-01");
}

// 模仿OSI协议，把ChatRequest和ChatResponse当作一层调用大模型得协议层，下一层就是Http
ChatResponse QwenProvider::Chat(const ChatRequest& request) {
    // http 请求
    HttpRequest http_request;
    http_request.url = base_url_ + "/v1/messages";
    http_request.timeout_seconds = timeout_;

    HeaderList headers = CreateHeaders();
    http_request.headers = headers.get();
    PrintRequestLog(http_request.url);

    http_request.post_data = Serialize(request);

    HttpResponse http_response = HttpClient::Post(http_request);

    LOG_DEBUG("=== Received response from Qwen API ===");

    if (http_response.failed()) {
        std::string error_msg = http_response.error.empty() ? http_response.body : http_response.error;
        if (http_response.retry_after_seconds > 0) {
            LOG_WARN("Qwen API HTTP {}: rate limited, retry-after={}s",
                     http_response.status_code, http_response.retry_after_seconds);
        } else {
            LOG_ERROR("Qwen API HTTP {}: {}", http_response.status_code,
                      error_msg.substr(0, 256));
        }
        throw std::runtime_error("Qwen API error (HTTP " +
                                 std::to_string(http_response.status_code) + "): " +
                                 error_msg);
    }

    ChatResponse response = Deserialize(http_response.body);

    // Record token usage
    if (response.usage.total_tokens > 0) {
        RecordTokenUsage(request.model, response.usage);
        LOG_INFO("Token usage: {} prompt, {} completion, {} total",
                 response.usage.prompt_tokens, response.usage.completion_tokens,
                 response.usage.total_tokens);
    }

    return response;
}

HeaderList QwenProvider::CreateHeaders() const {
    HeaderList headers;
    headers.append("Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers.append(auth_header.c_str());
    headers.append("anthropic-version: 2023-06-01");
    return headers;
}

// --- SSE Streaming support ---

// Qwen SSE stream handler - pushes response to callback via external worker
struct QwenStreamHandler : public SseStreamHandler {
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

    // Accumulated response for return value
    ChatResponse accumulated_response;

    explicit QwenStreamHandler(std::function<void(const ChatResponse&)> cb)
        : chat_response_callback(std::move(cb)) {}

    void OnEvent(const std::string& event_type, const std::string& data) {
        auto j = nlohmann::json::parse(data, nullptr, false);
        if (j.is_discarded()) return;

        if (event_type == "content_block_start") {
            current_block_index++;
            if (j.contains("content_block")) {
                const auto& block = j["content_block"];
                current_block_type = block.value("type", "");
                if (current_block_type == "tool_use") {
                    //LOG_DEBUG("response tool_use name: {}", block.dump());
                    PendingToolCall ptc;
                    ptc.id = block.value("id", "");
                    ptc.name = block.value("name", "");
                    pending_tool_calls.push_back(ptc);
                }
            }
        } else if (event_type == "content_block_delta") {
            if (j.contains("delta")) {
                const auto& delta = j["delta"];
                std::string delta_type = delta.value("type", "");

                if (delta_type == "text_delta") {
                    std::string text = delta.value("text", "");
                    if (!text.empty()) {
                        ChatResponse resp;
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

ChatResponse QwenProvider::ChatStream(const ChatRequest& request, std::function<void(const ChatResponse&)> callback) {
    QwenStreamHandler stream_handler(std::move(callback));

    HttpRequest stream_req;
    stream_req.url = base_url_ + "/v1/messages";
    stream_req.post_data = Serialize(request);
    stream_req.timeout_seconds = timeout_;
    stream_req.low_speed_limit = 1;
    stream_req.low_speed_time = 60;

    HeaderList headers = CreateHeaders();
    stream_req.headers = headers.get();
    stream_req.write_data = &stream_handler;

    LOG_DEBUG("Sending streaming request to Qwen API");
    PrintRequestLog(stream_req.url);

    HttpClient::Post(stream_req);

    // Return accumulated response
    return stream_handler.accumulated_response;
}

}  // namespace aicode
