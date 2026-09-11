// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace prosophor {

/// Represents a content block in a message
struct ContentSchema {
    std::string type;        // "text" | "tool_use" | "tool_result" | "thinking" | "image"
    std::string text;        // For text/thinking blocks
    std::string name;        // For tool_use blocks
    nlohmann::json input;    // For tool_use blocks; image 块存 {"path": <文件路径>}
    std::string tool_use_id; // For tool_result blocks
    std::string content;     // For tool_result blocks
    std::string signature;   // For thinking blocks (required by API)
    bool is_error = false;
};

/// System message block with optional cache control
struct SystemSchema {
    std::string type;            // "text"
    std::string text;            // System prompt text
    bool cache_control = false;  // Whether to cache this system message
};

/// Message schema structure for LLM communication
struct MessageSchema {
    std::string role;
    std::vector<ContentSchema> content;
    std::string summary;  // Running summary carried across requests (updated by LLM on each turn)
    std::string sender_id;    // IM 发送者 ID(飞书 open_id);群聊必填,私聊可空(空按 owner 兜底)
    std::string sender_name;  // 展示名,可空

    MessageSchema() = default;
    MessageSchema(std::string r, std::string text) : role(std::move(r)) {
        if (!text.empty())
            AddTextContent(text);
    }

    /// 带 sender 构造(IM 场景):sender 右值传入,消费式携带发言者归属
    MessageSchema(std::string r, std::string text, std::string sid, std::string sname)
        : role(std::move(r)), sender_id(std::move(sid)), sender_name(std::move(sname)) {
        if (!text.empty())
            AddTextContent(text);
    }

    /// 文本视图:拼 text 与 thinking 块;块间用空行分隔
    /// (thinking 段与正文段视觉分离,避免连成一片)。
    /// 流式帧中只有单块 = 单帧增量;终结消息中 = thinking + 正文全量。
    std::string text() const {
        std::string r;
        for (const auto& b : content) {
            if (b.type != "text" && b.type != "thinking") continue;
            if (!r.empty()) r += "\n\n";
            r += b.text;
        }
        return r;
    }

    // Convenience methods for building message content
    void AddTextContent(std::string text) {
        content.push_back({"text", std::move(text), "", "", {}, "", "", false});
    }

    void AddThinkingContent(std::string text, std::string sig = "") {
        content.push_back({"thinking", std::move(text), "", "", {}, "", std::move(sig), false});
    }

    /// 图像内容块（多模态模型专用）：path 为本地图像文件路径。
    /// 纯文本 provider 的序列化路径不识别本块 —— 由调用方保证只发给多模态 provider。
    void AddImageContent(std::string path) {
        content.push_back({"image", "", "",
                           nlohmann::json{{"path", std::move(path)}},
                           "", "", "", false});
    }

    void AddToolUseContent(const std::string& id, const std::string& name, nlohmann::json input) {
        content.push_back({"tool_use", "", name, std::move(input), id, "", "", false});
    }

    void AddToolResultContent(const std::string& id, const std::string& result, bool is_error = false) {
        content.push_back({"tool_result", "", "", {}, id, result, "", is_error});
    }
};

}  // namespace prosophor
