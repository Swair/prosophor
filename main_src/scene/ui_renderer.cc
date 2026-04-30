// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/ui_renderer.h"
#include "components/chat_panel.h"
#include "ui_component/input_panel.h"
#include "components/status_bar.h"
#include "media_engine/media_engine.h"
#include "common/log_wrapper.h"

namespace prosophor {

UIRenderer& UIRenderer::Instance() {
    static UIRenderer instance;
    return instance;
}

UIRenderer::UIRenderer() = default;
UIRenderer::~UIRenderer() = default;

// 更新布局（窗口大小改变时调用）
void UIRenderer::UpdateLayout() {
    float window_width = static_cast<float>(MediaCore::Instance().GetWindowWidth());
    float window_height = static_cast<float>(MediaCore::Instance().GetWindowHeight());

    // 使用统一布局配置
    float chat_panel_x = LayoutConfig::GetChatPanelX(window_width);
    float chat_panel_width = LayoutConfig::GetChatPanelWidth(window_width);
    float chat_panel_y = LayoutConfig::GetChatPanelY();
    float chat_panel_height = LayoutConfig::GetChatPanelHeight(window_height, input_area_height_, bottom_status_height_);

    float input_panel_x = chat_panel_x;
    float input_panel_y = chat_panel_height;
    float input_panel_width = chat_panel_width;

    float status_bar_x = 0.0f;
    float status_bar_y = window_height - bottom_status_height_ - 10.0f;
    float status_bar_width = window_width;

    // 更新组件位置和尺寸
    if (chat_panel_) {
        chat_panel_->SetPosition(chat_panel_x, chat_panel_y);
        chat_panel_->SetSize(chat_panel_width, chat_panel_height);
    }
    if (input_panel_) {
        input_panel_->SetPosition(input_panel_x, input_panel_y);
        input_panel_->SetSize(input_panel_width, input_area_height_);
    }
    if (status_bar_) {
        status_bar_->SetPosition(status_bar_x, status_bar_y);
        status_bar_->SetSize(status_bar_width, bottom_status_height_);
    }
}

// 界面面板布局定义
void UIRenderer::Initialize() {
    // 初始化聚合组件（位置会在 UpdateLayout 中设置）
    // 注意：初始尺寸会被 UpdateLayout() 覆盖，这里只是占位
    chat_panel_ = std::make_unique<ChatPanel>(0, 0, 100, 100);
    input_panel_ = std::make_unique<InputPanel>(0, 0, 100, input_area_height_);
    status_bar_ = std::make_unique<StatusBar>(0, 0, 100, bottom_status_height_);

    // 设置初始布局
    UpdateLayout();

    // 设置回调
    input_panel_->SetOnSubmit([this](const std::string& msg) {
        SubmitUserMessage(msg);
    });

    LOG_INFO("UIRenderer initialized with aggregated components.");
}

void UIRenderer::SetOnMessageSubmit(MessageSubmitCallback cb) {
    on_message_submit_ = cb;
}

void UIRenderer::SetStatePropsGetter(StatePropsGetter getter) {
    state_props_getter_ = getter;
    if (status_bar_) {
        status_bar_->SetStatePropsGetter(getter);
    }
}

void UIRenderer::SetAgentState(AgentRuntimeState state) {
    current_state_ = state;
}

void UIRenderer::Render() {
    if (!visible_) return;

    // 更新布局（响应窗口大小变化）
    UpdateLayout();

    // 渲染组件背景
    if (chat_panel_) chat_panel_->Render();
    if (input_panel_) input_panel_->Render();
    if (status_bar_) {
        status_bar_->Render();
        status_bar_->RenderContent(status_text_, current_state_);
    }
}

void UIRenderer::RenderImGui() {
    if (!visible_) return;

    // 渲染组件内容（与 Render() 顺序一致）
    if (chat_panel_) chat_panel_->RenderContent();
    if (input_panel_) input_panel_->RenderContent();
}

bool UIRenderer::ProcessInput(std::string& out_message) {
    if (input_panel_) {
        std::string input = input_panel_->GetText();
        if (!input.empty()) {
            input_panel_->SetText("");
            out_message = input;
            return true;
        }
    }
    return false;
}

void UIRenderer::SubmitUserMessage(const std::string& message) {
    LOG_DEBUG("SubmitUserMessage submitted: {}, {}", "user", message.c_str());
    SendToChatPanel("user", message);

    if (on_message_submit_) {
        on_message_submit_(message);
    }
}

void UIRenderer::SendToChatPanel(const std::string& role, const std::string& content) {
    if (chat_panel_) {
        chat_panel_->AddMessage(role, content);
    }
}

void UIRenderer::UpdateLastMessage(const std::string& content) {
    if (chat_panel_) {
        chat_panel_->UpdateLastMessage(content);
    }
}

void UIRenderer::StartAssistantMessage() {
    if (chat_panel_) {
        chat_panel_->StartAssistantMessage();
    }
}

void UIRenderer::ClearHistory() {
    if (chat_panel_) {
        chat_panel_->ClearMessages();
    }
}

void UIRenderer::SetStatusText(const std::string& status) {
    status_text_ = status;
}

void UIRenderer::SetVisible(bool visible) {
    visible_ = visible;
    if (chat_panel_) chat_panel_->SetVisible(visible);
    if (input_panel_) input_panel_->SetVisible(visible);
    if (status_bar_) status_bar_->SetVisible(visible);
}

void UIRenderer::RenderFloatingText(const std::string& text, float x, float y,
                                     uint8_t r, uint8_t g, uint8_t b, float alpha) {
    static const char* kFontPath = "C:/Windows/Fonts/msyh.ttc";
    MediaUtil::DrawTextRect(text, x, y, 300, 14, r, g, b,
                             static_cast<uint8_t>(alpha * 255), kFontPath);
}

}  // namespace prosophor
