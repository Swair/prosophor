// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ui_component/ui_container.h"
#include "media/imgui_widget.h"

namespace prosophor {

UIContainer::UIContainer(float x, float y, float width, float height, PanelStyle style)
    : UIPanel(x, y, width, height, style) {
}

void UIContainer::SetContentCallback(ContentCallback cb) {
    content_callback_ = cb;
}

void UIContainer::RenderContent(const std::string& name) {
    if (!visible_ || !content_callback_) return;

    using namespace imgui_widget;

    float content_x = GetContentX();
    float content_y = GetContentY();
    float content_width = GetContentWidth();
    float content_height = GetContentHeight();

    SetImGuiNextWindowPos(content_x, content_y);
    SetImGuiNextWindowSize(content_width, content_height);
    SetImGuiNextWindowBgAlpha(0.0f);

    ImGuiBegin(name.c_str(), nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

    content_callback_(content_x, content_y, content_width, content_height);

    ImGuiEnd();
}

}  // namespace prosophor
