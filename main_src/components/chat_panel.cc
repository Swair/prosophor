// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "components/chat_panel.h"
#include "media/imgui_widget.h"
#include "media/colors.h"
#include "common/log_wrapper.h"

namespace prosophor {

// ============================================================================
// ChatPanel 实现
// ============================================================================

// 构造函数：初始化容器和滚动窗口
ChatPanel::ChatPanel(float x, float y, float width, float height) {
    container_ = std::make_unique<UIContainer>(x, y, width, height, PanelStyle::ChatPanel());
    scroll_window_ = std::make_unique<imgui_widget::ScrollWindow>(0, 0, 0, 0);
}

// 析构函数（在 .cc 中实现，避免 unique_ptr 不完整类型问题）
ChatPanel::~ChatPanel() = default;

// 设置面板位置
void ChatPanel::SetPosition(float x, float y) {
    container_->SetPosition(x, y);
}

// 设置面板尺寸
void ChatPanel::SetSize(float width, float height) {
    container_->SetSize(width, height);
}

// 渲染背景（SDL 层）
void ChatPanel::Render() const {
    if (!visible_) return;
    container_->Render();
}

// 渲染聊天内容（ImGui 层）
void ChatPanel::RenderContent() {
    if (!visible_) return;

    container_->SetContentCallback([this](float /*cx*/, float /*cy*/, float content_width, float content_height) {
        scroll_window_->SetPosition(container_->GetContentX(), container_->GetContentY());
        scroll_window_->SetSize(content_width, content_height);
        scroll_window_->Begin("______________________________", &Colors::LightBlue);

        for (size_t i = 0; i < messages_.size(); i++) {
            RenderMessage(messages_[i], i);
        }

        scroll_window_->End();
    });

    container_->RenderContent("ChatPanel");
}

// 渲染单条消息气泡
void ChatPanel::RenderMessage(const ChatMessage& msg, size_t index) {
    using namespace imgui_widget;

    // 消息之间添加 8 像素间距
    if (index > 0) {
        Dummy(0, 8);
    }

    // 渲染角色标签（灰色显示）
    ImGuiTextColored(Colors::Orange, msg.role.c_str());

    // 添加内容间距
    Dummy(0, 5);

    // 渲染消息内容文本（黄色，自动换行）
    ImGuiTextWrapped(msg.content.c_str(), scroll_window_->GetWidth(), Colors::Yellow);

    // 消息底部间距
    Dummy(0, 5);
}

// 添加消息到聊天列表
void ChatPanel::AddMessage(const std::string& role, const std::string& content) {
    ChatMessage msg;
    msg.role = role;
    msg.name = (role == "user") ? "用户" : (role == "assistant" ? "助手" : "系统");
    msg.content = content;
    msg.timestamp = 0;

    messages_.push_back(msg);

    // 限制最大消息数量，避免内存无限增长
    static constexpr size_t MAX_MESSAGES = 50;
    if (messages_.size() > MAX_MESSAGES) {
        messages_.erase(messages_.begin());
    }

    // 自动滚动到底部，显示最新消息
    scroll_window_->ScrollToBottom();
}

// 开始流式响应时，创建一条空的 Agent 消息（用于后续追加内容）
void ChatPanel::StartAssistantMessage() {
    // 如果最后一条已经是 Agent 消息，不重复创建
    if (!messages_.empty() && messages_.back().role == "assistant") {
        return;
    }
    AddMessage("assistant", "");
}

// 更新最后一条消息的内容（用于流式响应）
void ChatPanel::UpdateLastMessage(const std::string& content) {
    // 必须有至少一条消息，且最后一条是 Agent 消息
    if (messages_.empty() || messages_.back().role != "assistant") {
        return;  // 忽略非法调用
    }

    // 追加内容到最后一条消息
    messages_.back().content += content;
    scroll_window_->ScrollToBottom();
}

// 清空所有消息
void ChatPanel::ClearMessages() {
    messages_.clear();
}

// 滚动到底部
void ChatPanel::ScrollToBottom() {
    scroll_window_->ScrollToBottom();
}

// 检查是否已滚动到底部
bool ChatPanel::IsScrolledToBottom() const {
    return scroll_window_->IsScrolledToBottom();
}

}  // namespace prosophor
