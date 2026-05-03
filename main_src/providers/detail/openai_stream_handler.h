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

// OpenAI-compatible SSE stream handler
// Format:
//   data: {"choices":[{"delta":{"reasoning_content":"...","content":"..."}}]}
//   data: [DONE]
struct OpenAIStreamHandler : public SseStreamHandler {
    std::function<void(StreamEvent, std::string)> stream_callback;

    // Accumulated response for return value
    ChatResponse accumulated_response;

    explicit OpenAIStreamHandler(std::function<void(StreamEvent, std::string)> cb)
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

    void OnEvent(const std::string& /*event_type*/, const std::string& data) override {
        if (data.empty()) {
            return;
        }

        if (data == "[DONE]") {
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
            return;
        }

        auto chunk = nlohmann::json::parse(data, nullptr, false);
        if (chunk.is_discarded()) {
            return;
        }

        if (!chunk.contains("choices") || chunk["choices"].empty()) {
            return;
        }

        const auto& choice = chunk["choices"][0];
        const auto& delta = choice.contains("delta") && choice["delta"].is_object()
            ? choice["delta"] : nlohmann::json::object();
        std::string finish;
        auto fr_it = choice.find("finish_reason");
        if (fr_it != choice.end() && fr_it->is_string()) {
            finish = fr_it->get<std::string>();
        }

        if (delta.empty()) {
            if (!finish.empty()) {
                accumulated_response.stop_reason = finish;
            }
            return;
        }

        // Parse reasoning_content (DeepSeek/Qwen style)
        auto rc_it = delta.find("reasoning_content");
        if (rc_it != delta.end() && rc_it->is_string()) {
            std::string rc = rc_it->get<std::string>();
            if (!rc.empty()) {
                TransitionPhase(StreamPhase::kThinking);
                accumulated_response.has_thinking = true;
                accumulated_response.content_thinking += rc;
                stream_callback(StreamEvent::kThinkingDelta, std::move(rc));
            }
        }

        // Parse regular content
        auto cc_it = delta.find("content");
        if (cc_it != delta.end() && cc_it->is_string()) {
            std::string cc = cc_it->get<std::string>();
            if (!cc.empty()) {
                TransitionPhase(StreamPhase::kContent);
                accumulated_response.content_text += cc;
                stream_callback(StreamEvent::kContentDelta, std::move(cc));
            }
        }

        // Parse tool calls in streaming
        {
            auto tc_arr_it = delta.find("tool_calls");
            if (tc_arr_it != delta.end() && tc_arr_it->is_array()) {
                TransitionPhase(StreamPhase::kToolCalls);
                for (const auto& tc : *tc_arr_it) {
                    auto func_it = tc.find("function");
                    if (func_it == tc.end()) continue;
                    const auto& func = *func_it;

                    auto id_it = tc.find("id");
                    if (id_it != tc.end() && id_it->is_string()) {
                        std::string id = id_it->get<std::string>();
                        if (!id.empty()) {
                            PendingToolCall ptc;
                            ptc.id = std::move(id);
                            auto name_it = func.find("name");
                            if (name_it != func.end() && name_it->is_string()) {
                                ptc.name = name_it->get<std::string>();
                            }
                            pending_tool_calls.push_back(std::move(ptc));
                        }
                    }

                    if (!pending_tool_calls.empty()) {
                        auto args_it = func.find("arguments");
                        if (args_it != func.end() && args_it->is_string()) {
                            std::string args_str = args_it->get<std::string>();
                            if (!args_str.empty()) {
                                pending_tool_calls.back().arguments += args_str;
                            }
                        }
                    }
                }
            }
        }

        if (!finish.empty()) {
            accumulated_response.stop_reason = finish;
        }

        if (chunk.contains("usage")) {
            const auto& u = chunk["usage"];
            accumulated_response.usage.prompt_tokens = u.value("prompt_tokens", 0);
            accumulated_response.usage.completion_tokens = u.value("completion_tokens", 0);
            accumulated_response.usage.total_tokens = u.value("total_tokens", 0);
        }
    }
};

}  // namespace prosophor
