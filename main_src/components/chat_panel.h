// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "media_engine/media_engine.h"
#include "ui_types.h"
#include <string>
#include <vector>
#include <memory>

namespace prosophor {

/// 聊天面板组件 - 使用独立 ScrollWindow
class ChatPanel {
public:
    ChatPanel(float x, float y, float width, float height);
    ~ChatPanel();

    void SetPosition(float x, float y);
    void SetSize(float width, float height);

    void Render() const;
    void RenderContent();

    void AddMessage(const std::string& role, const std::string& content);
    void StartAssistantMessage();  // 开始流式响应时创建空消息
    void UpdateLastMessage(const std::string& content);
    void ClearMessages();

    void ScrollToBottom();
    bool IsScrolledToBottom() const;

    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

private:
    std::unique_ptr<UIContainer> container_;
    std::unique_ptr<imgui_widget::ScrollWindow> scroll_window_;
    std::vector<ChatMessage> messages_;
    bool visible_ = true;

    void RenderMessage(const ChatMessage& msg, size_t index);
};

}  // namespace prosophor
