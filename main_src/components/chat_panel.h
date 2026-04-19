// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ui_types.h"
#include "colors.h"
#include "ui_panel.h"
#include "media/imgui_widget.h"
#include <string>
#include <vector>
#include <memory>

namespace aicode {

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
    float x_ = 0, y_ = 0;       // ChatPanel 位置
    float width_ = 0, height_ = 0;  // ChatPanel 尺寸
    std::unique_ptr<imgui_widget::ScrollWindow> scroll_window_;
    std::vector<ChatMessage> messages_;
    bool visible_ = true;

    void RenderMessage(const ChatMessage& msg, size_t index);
};

}  // namespace aicode
