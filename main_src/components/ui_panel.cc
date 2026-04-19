// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "components/ui_panel.h"
#include "drawer.h"
#include "media/imgui_widget.h"
#include "common/log_wrapper.h"

namespace aicode {

// ============================================================================
// PanelStyle 预设样式
// ============================================================================

PanelStyle PanelStyle::Default() {
    PanelStyle style;
    style.background_color = Color(20, 20, 20, 200);
    style.border_color = Color(80, 80, 80, 255);
    style.has_border = true;
    style.padding = 10.0f;
    return style;
}

PanelStyle PanelStyle::InputField() {
    PanelStyle style;
    style.background_color = Color(35, 35, 35, 220);
    style.border_color = Colors::UiPanelBorder;
    style.padding = 8.0f;
    style.has_border = true;
    return style;
}

PanelStyle PanelStyle::StatusBar() {
    PanelStyle style;
    style.background_color = Colors::UiStatusBarBg;
    style.border_color = Colors::UiStatusBarBorder;
    style.has_border = true;
    style.padding = 5.0f;
    return style;
}

PanelStyle PanelStyle::ChatPanel() {
    PanelStyle style;
    style.background_color = Colors::UiPanelBg;
    style.border_color = Colors::UiPanelBorder;
    style.has_border = true;
    style.padding = 8.0f;
    return style;
}

PanelStyle PanelStyle::MessageUser() {
    PanelStyle style;
    style.background_color = Colors::UiMessageUserBg;
    style.border_color = Colors::UiMessageUserBorder;
    style.padding = 6.0f;
    style.has_border = false;
    return style;
}

PanelStyle PanelStyle::MessageAgent() {
    PanelStyle style;
    style.background_color = Colors::UiMessageAgentBg;
    style.border_color = Colors::UiMessageAgentBorder;
    style.padding = 6.0f;
    style.has_border = false;
    return style;
}

PanelStyle PanelStyle::MessageSystem() {
    PanelStyle style;
    style.background_color = Color(60, 60, 60, 200);
    style.border_color = Color(100, 100, 100, 255);
    style.padding = 6.0f;
    style.has_border = false;
    return style;
}

PanelStyle PanelStyle::Card() {
    PanelStyle style;
    style.background_color = Color(25, 25, 25, 230);
    style.border_color = Color(90, 90, 90, 255);
    style.has_border = true;
    style.corner_radius = 8.0f;
    style.padding = 12.0f;
    return style;
}

// ============================================================================
// UIPanel 实现
// ============================================================================

UIPanel::UIPanel(float x, float y, float width, float height, PanelStyle style)
    : x_(x), y_(y), width_(width), height_(height), style_(style) {
}

void UIPanel::RenderBackground() const {
    if (!visible_) return;
    ::Drawer::Instance().DrawFilledRectWithBorder(
        x_, y_, width_, height_,
        style_.background_color, Colors::Transparent);
}

void UIPanel::RenderBorder() const {
    if (!visible_ || !style_.has_border) return;
    ::Drawer::Instance().DrawFilledRectWithBorder(
        x_, y_, width_, height_,
        Colors::Transparent, style_.border_color);
}

void UIPanel::Render() const {
    if (!visible_) return;
    RenderBackground();
    RenderBorder();
}

float UIPanel::GetContentX() const {
    return x_ + style_.padding;
}

float UIPanel::GetContentY() const {
    if (style_.has_header) {
        return y_ + style_.header_height + style_.padding;
    }
    return y_ + style_.padding;
}

float UIPanel::GetContentWidth() const {
    return width_ - style_.padding * 2;
}

float UIPanel::GetContentHeight() const {
    if (style_.has_header) {
        return height_ - style_.header_height - style_.padding * 2;
    }
    return height_ - style_.padding * 2;
}

void UIPanel::SetPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void UIPanel::SetSize(float width, float height) {
    width_ = width;
    height_ = height;
}

void UIPanel::SetStyle(const PanelStyle& style) {
    style_ = style;
}

void UIPanel::RenderFloatText(const std::string& text, FloatPosition pos,
                               float offset_x, float offset_y) const {
    if (!visible_) return;

    float draw_x = x_;
    float draw_y = y_;

    switch (pos) {
        case FloatPosition::TopLeft:
            draw_x += offset_x;
            draw_y += offset_y;
            break;
        case FloatPosition::TopRight:
            draw_x += width_ - offset_x - text.length() * 12;
            draw_y += offset_y;
            break;
        case FloatPosition::BottomLeft:
            draw_x += offset_x;
            draw_y += height_ - offset_y - 14;
            break;
        case FloatPosition::BottomRight:
            draw_x += width_ - offset_x - text.length() * 12;
            draw_y += height_ - offset_y - 14;
            break;
        case FloatPosition::Center:
            draw_x += (width_ - text.length() * 12) / 2;
            draw_y += (height_ - 14) / 2;
            break;
    }

    for (size_t i = 0; i < text.size() && i < 50; i++) {
        char c = text[i];
        if (c >= 32 && c < 127) {
            ::Drawer::Instance().DrawFillRect(draw_x + i * 12, draw_y, 8, 14,
                                              Colors::TextGray);
        }
    }
}

// ============================================================================
// UIContainer 实现
// ============================================================================

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


HeaderBar::HeaderBar(float x, float y, float width, float height)
    : x_(x), y_(y), width_(width), height_(height) {
}

void HeaderBar::Render(const std::string& title, Color bg_color) const {
    ::Drawer::Instance().DrawFilledRectWithBorder(
        x_, y_, width_, height_,
        bg_color, Colors::Transparent);

    for (size_t i = 0; i < title.size() && i < 50; i++) {
        char c = title[i];
        if (c >= 32 && c < 127) {
            ::Drawer::Instance().DrawFillRect(x_ + 10 + i * 12, y_ + 7, 8, 14,
                                              Colors::TextGray);
        }
    }
}


}  // namespace aicode
