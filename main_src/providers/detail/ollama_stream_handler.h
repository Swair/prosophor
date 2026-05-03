// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <string>
#include <nlohmann/json.hpp>
#include "common/curl_client.h"
#include "common/log_wrapper.h"
#include "providers/llm_provider.h"

namespace prosophor {

// Ollama NDJSON stream handler — parses line-delimited JSON with done/message fields
struct OllamaStreamHandler : public StreamHandler {
    std::function<void(StreamEvent, std::string)> stream_callback;
    std::string buffer;

    // Accumulated response for return value
    ChatResponse accumulated_response;

    explicit OllamaStreamHandler(std::function<void(StreamEvent, std::string)> cb)
        : stream_callback(std::move(cb)) {}

    void OnEvent(const std::string&, const std::string&) override {}
    std::string& Buffer() override { return buffer; }

    void EndCurrentPhase() {
        if (phase == StreamPhase::kThinking) {
            stream_callback(StreamEvent::kThinkingEnd, "");
        } else if (phase == StreamPhase::kContent) {
            stream_callback(StreamEvent::kContentEnd, "");
        }
        phase = StreamPhase::kNone;
    }

    void TransitionPhase(StreamPhase target) {
        if (phase == target) {
            return;
        }
        EndCurrentPhase();
        phase = target;
        if (phase == StreamPhase::kThinking) {
            stream_callback(StreamEvent::kThinkingStart, "");
        } else if (phase == StreamPhase::kContent) {
            stream_callback(StreamEvent::kContentStart, "");
        }
    }

    void OnLine(const std::string& line) override {
        if (line.empty()) {
            return;
        }

        auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) {
            return;
        }

        // Check for API error
        if (j.contains("error")) {
            std::string err = j["error"].is_string() ? j["error"].get<std::string>()
                                                     : j["error"].dump();
            accumulated_response.error_msg = err;
            stream_callback(StreamEvent::kError, std::move(err));
            return;
        }

        // Check for done signal
        if (j.value("done", false)) {
            EndCurrentPhase();

            accumulated_response.stop_reason = "end_turn";
            std::string done_reason = j.value("done_reason", "");
            if (done_reason == "tool_calls") {
                accumulated_response.stop_reason = "tool_use";
            } else if (done_reason == "stop") {
                accumulated_response.stop_reason = "end_turn";
            } else if (done_reason == "length") {
                accumulated_response.stop_reason = "max_tokens";
            }
            if (j.contains("usage")) {
                const auto& usage = j["usage"];
                accumulated_response.usage.prompt_tokens = usage.value("prompt_tokens", 0);
                accumulated_response.usage.completion_tokens = usage.value("completion_tokens", 0);
                accumulated_response.usage.total_tokens = accumulated_response.usage.prompt_tokens + accumulated_response.usage.completion_tokens;
            }
            return;
        }

        // Parse message content
        if (j.contains("message")) {
            const auto& msg = j["message"];

            // Parse thinking content
            if (msg.contains("thinking")) {
                accumulated_response.has_thinking = true;
                TransitionPhase(StreamPhase::kThinking);
                std::string thinking = msg.value("thinking", "");
                if (!thinking.empty()) {
                    accumulated_response.content_thinking += thinking;
                    stream_callback(StreamEvent::kThinkingDelta, std::move(thinking));
                }
            }

            // Parse regular content
            if (msg.contains("content")) {
                std::string content = msg.value("content", "");
                if (!content.empty()) {
                    TransitionPhase(StreamPhase::kContent);
                    accumulated_response.content_text += content;
                    stream_callback(StreamEvent::kContentDelta, std::move(content));
                }
            }

            // Parse tool calls in streaming
            if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                TransitionPhase(StreamPhase::kToolCalls);
                for (const auto& tc : msg["tool_calls"]) {
                    if (tc.contains("function")) {
                        const auto& func = tc["function"];
                        std::string name = func.value("name", "");
                        std::string id = tc.value("id", "");
                        nlohmann::json args = nlohmann::json::object();
                        if (func.contains("arguments")) {
                            if (func["arguments"].is_object()) {
                                args = func["arguments"];
                            } else if (func["arguments"].is_string()) {
                                auto parsed = nlohmann::json::parse(func["arguments"].get<std::string>(), nullptr, false);
                                if (!parsed.is_discarded()) {
                                    args = parsed;
                                }
                            }
                        }
                        accumulated_response.AddToolCall(id, name, args);
                    }
                }
                accumulated_response.stop_reason = "tool_use";
            }
        }
    }
};

}  // namespace prosophor
