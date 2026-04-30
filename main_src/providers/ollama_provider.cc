// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/ollama_provider.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"
#include "common/string_utils.h"
#include "common/curl_client.h"

using namespace nlohmann;

namespace prosophor {

OllamaProvider::OllamaProvider() {
    LOG_DEBUG("OllamaProvider initialized");
}

std::vector<std::string> OllamaProvider::GetSupportedModels() const {
    // Fetch available models from Ollama API
    std::vector<std::string> models;

    HttpRequest req;
    req.url = "http://localhost:11434/api/tags";
    req.timeout_seconds = 10;

    HttpResponse resp = HttpClient::Get(req);

    if (resp.success()) {
        try {
            auto j = json::parse(resp.body);
            if (j.contains("models")) {
                for (const auto& m : j["models"]) {
                    models.push_back(m.value("name", ""));
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse Ollama models: {}", e.what());
        }
    } else {
        std::string error_msg = resp.error.empty() ? resp.body : resp.error;
        LOG_ERROR("Failed to fetch Ollama models: HTTP {} - {}", resp.status_code, error_msg);
    }

    return models;
}

nlohmann::json OllamaProvider::SerializeMessage(const MessageSchema& msg) const {
    nlohmann::json msg_json = nlohmann::json::object();
    msg_json["role"] = msg.role;

    // Build content string
    std::string content;
    for (const auto& block : msg.content) {
        if (block.type == "text" || block.type == "thinking") {
            if (!content.empty()) content += "\n";
            content += block.text;
        } else if (block.type == "tool_use") {
            // Ollama doesn't support tool_use in messages the same way
            // Convert to text representation
            if (!content.empty()) content += "\n";
            content += "[Tool call: " + block.name + " with id " + block.tool_use_id + "]";
        } else if (block.type == "tool_result") {
            if (!content.empty()) content += "\n";
            content += "[Tool result: " + block.content + "]";
        }
    }

    msg_json["content"] = content;
    return msg_json;
}

nlohmann::json OllamaProvider::SerializeTools(const std::vector<ToolsSchema>& tools) const {
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& tool : tools) {
        nlohmann::json tool_json = nlohmann::json::object();
        tool_json["type"] = "function";

        nlohmann::json func_json = nlohmann::json::object();
        func_json["name"] = tool.name;
        func_json["description"] = tool.description;

        // Ollama uses OpenAI-compatible format
        if (!tool.input_schema.is_null()) {
            func_json["parameters"] = tool.input_schema;
        } else {
            // Default empty parameters
            func_json["parameters"] = R"({
                "type": "object",
                "properties": {},
                "required": []
            })"_json;
        }

        tool_json["function"] = func_json;
        arr.push_back(tool_json);
    }

    return arr;
}

std::string OllamaProvider::Serialize(const ChatRequest& request) const {
    nlohmann::json payload = nlohmann::json::object();

    payload["model"] = request.model;
    payload["stream"] = request.stream;

    // Serialize messages
    nlohmann::json messages = nlohmann::json::array();
    for (const auto& msg : request.messages) {
        messages.push_back(SerializeMessage(msg));
    }
    payload["messages"] = messages;

    // Serialize tools (OpenAI format)
    if (!request.tools.empty()) {
        payload["tools"] = SerializeTools(request.tools);
        if (request.tool_choice_auto) {
            payload["tool_choice"] = "auto";
        }
    }

    // Standard parameters
    payload["max_tokens"] = request.max_tokens;
    payload["temperature"] = request.temperature;

    LOG_DEBUG("Ollama request payload: {}", payload.dump(4));

    return payload.dump();
}

ChatResponse OllamaProvider::Deserialize(const std::string& json_str) const {
    nlohmann::json response_json;

    try {
        #ifdef _WIN32
        std::string utf8_json = ConvertToUtf8(json_str);
        response_json = nlohmann::json::parse(utf8_json);
        #else
        response_json = nlohmann::json::parse(json_str);
        #endif

        LOG_DEBUG("Ollama response: {}", response_json.dump(4));
    } catch (...) {
        LOG_ERROR("Failed to parse Ollama response: {}", json_str);
        throw;
    }

    ChatResponse result;

    // Parse message content
    if (response_json.contains("message")) {
        const auto& msg = response_json["message"];
        if (msg.contains("content")) {
            result.content_text = msg.value("content", "");
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
                            // args = nlohmann::json::parse(func["arguments"], nullptr, R"({})"_json);
                        }
                    }

                    result.AddToolCall(id, name, args);
                }
            }
        }
    }

    // Parse finish reason
    std::string finish_reason = response_json.value("finish_reason", "");
    if (finish_reason == "stop") {
        result.stop_reason = "end_turn";
    } else if (finish_reason == "tool_calls") {
        result.stop_reason = "tool_use";
    } else if (finish_reason == "length") {
        result.stop_reason = "max_tokens";
    } else if (!finish_reason.empty()) {
        result.stop_reason = finish_reason;
    }

    // Parse usage
    if (response_json.contains("usage")) {
        const auto& usage = response_json["usage"];
        result.usage.prompt_tokens = usage.value("prompt_tokens", 0);
        result.usage.completion_tokens = usage.value("completion_tokens", 0);
        result.usage.total_tokens = result.usage.prompt_tokens + result.usage.completion_tokens;
    }

    return result;
}

void OllamaProvider::PrintRequestLog(const ChatRequest& request) const {
    LOG_DEBUG("=== Sending request to Ollama API ===");
    LOG_DEBUG("URL: {}", request.base_url);
    LOG_DEBUG("Model: {}", request.model);
    LOG_DEBUG("Content-Type: application/json");
}

HeaderList OllamaProvider::CreateHeaders(const ChatRequest& /*request*/) const {
    HeaderList headers;
    headers.append("Content-Type: application/json");
    return headers;
}

// Streaming support for Ollama (NDJSON format)
struct OllamaStreamHandler : public StreamHandler {
    std::function<void(const ChatResponse&)> chat_response_callback;
    std::string buffer;

    // Accumulated response for return value
    ChatResponse accumulated_response;

    explicit OllamaStreamHandler(std::function<void(const ChatResponse&)> cb)
        : chat_response_callback(std::move(cb)) {}

    std::string& Buffer() override { return buffer; }

    void OnLine(const std::string& line) override {
        if (line.empty()) return;

        try {
            auto j = nlohmann::json::parse(line);

            // Check for done signal
            if (j.value("done", false)) {
                ChatResponse end_resp;
                end_resp.is_stream_end = true;
                end_resp.stop_reason = "end_turn";
                chat_response_callback(end_resp);
                return;
            }

            ChatResponse resp;

            // Parse message content
            if (j.contains("message")) {
                const auto& msg = j["message"];
                if (msg.contains("content")) {
                    std::string content = msg.value("content", "");
                    if (!content.empty()) {
                        resp.content_text = content;
                        accumulated_response.content_text += content;
                        chat_response_callback(resp);
                    }
                }

                // Parse tool calls in streaming
                if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                    for (const auto& tc : msg["tool_calls"]) {
                        if (tc.contains("function")) {
                            const auto& func = tc["function"];
                            std::string name = func.value("name", "");
                            std::string id = tc.value("id", "");

                            nlohmann::json args;
                            // if (func.contains("arguments")) {
                            //     if (func["arguments"].is_object()) {
                            //         args = func["arguments"];
                            //     } else if (func["arguments"].is_string()) {
                            //         args = nlohmann::json::parse(func["arguments"], nullptr, R"({})"_json);
                            //     }
                            // }

                            resp.AddToolCall(id, name, args);
                            accumulated_response.AddToolCall(id, name, args);
                        }
                    }
                    if (resp.HasToolCalls()) {
                        resp.stop_reason = "tool_use";
                        accumulated_response.stop_reason = "tool_use";
                        chat_response_callback(resp);
                    }
                }
            }

            // Parse usage if present
            if (j.contains("usage")) {
                const auto& usage = j["usage"];
                resp.usage.prompt_tokens = usage.value("prompt_tokens", 0);
                resp.usage.completion_tokens = usage.value("completion_tokens", 0);
                resp.usage.total_tokens = resp.usage.prompt_tokens + resp.usage.completion_tokens;
                accumulated_response.usage = resp.usage;
            }

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse Ollama stream chunk: {}", e.what());
        }
    }
};

ChatResponse OllamaProvider::ChatStream(const ChatRequest& request,
                                 std::function<void(const ChatResponse&)> callback) {
    OllamaStreamHandler stream_handler(std::move(callback));
    PrintRequestLog(request);
    ExecuteStream(request, &stream_handler, 180);
    return stream_handler.accumulated_response;
}

}  // namespace prosophor
