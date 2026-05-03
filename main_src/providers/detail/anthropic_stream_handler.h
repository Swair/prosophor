// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <string>
#include <nlohmann/json.hpp>
#include "common/curl_client.h"
#include "providers/llm_provider.h"

namespace prosophor {

// Anthropic SSE stream handler — parses content_block_start/delta/stop events
struct AnthropicStreamHandler : public SseStreamHandler {
    std::function<void(StreamEvent, std::string)> stream_callback;
    std::string stop_reason;
    TokenUsageSchema usage;

    // Accumulated response for return value
    ChatResponse accumulated_response;

    explicit AnthropicStreamHandler(std::function<void(StreamEvent, std::string)> cb)
        : stream_callback(std::move(cb)) {}

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

    void OnEvent(const std::string& event_type, const std::string& data) override {
        auto j = nlohmann::json::parse(data, nullptr, false);
        if (j.is_discarded()) {
            return;
        }

        if (event_type == "content_block_start") {
            if (j.contains("content_block")) {
                const auto& block = j["content_block"];
                std::string block_type = block.value("type", "");
                if (block_type == "thinking") {
                    TransitionPhase(StreamPhase::kThinking);
                    accumulated_response.has_thinking = true;
                } else if (block_type == "text") {
                    TransitionPhase(StreamPhase::kContent);
                } else if (block_type == "tool_use") {
                    TransitionPhase(StreamPhase::kToolCalls);
                    PendingToolCall ptc;
                    ptc.id = block.value("id", "");
                    ptc.name = block.value("name", "");
                    pending_tool_calls.push_back(ptc);
                }
            }
        } else if (event_type == "content_block_stop") {
            EndCurrentPhase();
        } else if (event_type == "content_block_delta") {
            if (j.contains("delta")) {
                const auto& delta = j["delta"];
                std::string delta_type = delta.value("type", "");

                if (delta_type == "thinking_delta") {
                    std::string text = delta.value("thinking", "");
                    if (!text.empty()) {
                        accumulated_response.content_thinking += text;
                        stream_callback(StreamEvent::kThinkingDelta, std::move(text));
                    }
                } else if (delta_type == "signature_delta") {
                    std::string sig = delta.value("signature", "");
                    if (!sig.empty()) {
                        accumulated_response.thinking_signature = sig;
                    }
                } else if (delta_type == "text_delta") {
                    std::string text = delta.value("text", "");
                    if (!text.empty()) {
                        accumulated_response.content_text += text;
                        stream_callback(StreamEvent::kContentDelta, std::move(text));
                    }
                } else if (delta_type == "input_json_delta") {
                    if (!pending_tool_calls.empty()) {
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
            EndCurrentPhase();

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
        }
    }
};

}  // namespace prosophor
