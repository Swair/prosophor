// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace aicode {

/// Represents a content block in a message
struct ContentSchema {
    std::string type;        // "text" | "tool_use" | "tool_result" | "thinking"
    std::string text;        // For text/thinking blocks
    std::string tool_use_id;
    std::string name;        // For tool_use blocks
    nlohmann::json input;    // For tool_use blocks
    std::string content;     // For tool_result blocks
    bool is_error = false;
};

/// Message schema structure for LLM communication
struct MessageSchema {
    std::string role;
    std::vector<ContentSchema> content;

    MessageSchema() = default;
    MessageSchema(std::string role, std::string text) : role(std::move(role)) {
        if (!text.empty())
            AddTextContent(text);
    }

    std::string text() const {
        std::string r;
        for (const auto& b : content)
            if (b.type == "text" || b.type == "thinking")
                r += b.text;
        return r;
    }

    // Convenience methods for building message content
    void AddTextContent(std::string text) {
        content.push_back({"text", std::move(text), "", "", {}, "", false});
    }

    void AddThinkingContent(std::string text) {
        content.push_back({"thinking", std::move(text), "", "", {}, "", false});
    }

    void AddToolUseContent(const std::string& id, const std::string& name, nlohmann::json input) {
        content.push_back({"tool_use", "", id, name, std::move(input), "", false});
    }

    void AddToolResultContent(const std::string& id, const std::string& result, bool is_error = false) {
        content.push_back({"tool_result", "", id, "", {}, result, is_error});
    }
};

}  // namespace aicode
