// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "components/status_bar.h"
#include "drawer.h"
#include "common/log_wrapper.h"

namespace aicode {

StatusBar::StatusBar(float x, float y, float width, float height) {
    panel_ = std::make_unique<UIPanel>(x, y, width, height, PanelStyle::StatusBar());
}

void StatusBar::SetPosition(float x, float y) {
    panel_->SetPosition(x, y);
}

void StatusBar::SetSize(float width, float height) {
    panel_->SetSize(width, height);
}

void StatusBar::Render() const {
    if (!visible_) return;
    panel_->Render();
}

void StatusBar::RenderContent(const std::string& status_text, AgentRuntimeState state) {
    if (!visible_) return;

    StateVisualProps state_props;
    if (state_props_getter_) {
        state_props = state_props_getter_(state);
    } else {
        state_props = {128, 128, 128, 255, "Unknown"};
    }

    float status_y = panel_->GetY() + 5.0f;

    // 状态图标（根据状态显示不同图标）
    const char* icon = "";
    switch (state) {
        case AgentRuntimeState::IDLE: icon = "○"; break;
        case AgentRuntimeState::THINKING: icon = "◐"; break;
        case AgentRuntimeState::EXECUTING_TOOL: icon = "⚙"; break;
        case AgentRuntimeState::WAITING_PERMISSION: icon = "⏳"; break;
        case AgentRuntimeState::STATE_ERROR: icon = "⚠"; break;
        case AgentRuntimeState::COMPLETE: icon = "✓"; break;
        default: icon = "○"; break;
    }

    // 绘制状态图标
    for (size_t i = 0; i < 2 && i < strlen(icon); i++) {
        ::Drawer::Instance().DrawFillRect(20 + i * 12, status_y, 10, 16,
            Color(state_props.r, state_props.g, state_props.b, 255));
    }

    // 状态名称
    const std::string& state_name = state_props.name;
    for (size_t i = 0; i < state_name.size() && i < 15; i++) {
        char c = state_name[i];
        if (c >= 32 && c < 127) {
            ::Drawer::Instance().DrawFillRect(45 + i * 12, status_y, 8, 14, Colors::LightGray);
        }
    }

    // 状态文本
    for (size_t i = 0; i < status_text.size() && i < 30; i++) {
        char c = status_text[i];
        if (c >= 32 && c < 127) {
            ::Drawer::Instance().DrawFillRect(180 + i * 12, status_y, 8, 14, Colors::Gray);
        }
    }

    // ESC 提示
    float window_width = panel_->GetWidth();
    ::Drawer::Instance().DrawFillRect(window_width - 100, status_y, 80, 15, Colors::DarkGray);
}

void StatusBar::SetStatePropsGetter(StatePropsGetter getter) {
    state_props_getter_ = getter;
}

}  // namespace aicode
