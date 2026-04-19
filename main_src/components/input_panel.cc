// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "components/input_panel.h"
#include "media/imgui_widget.h"
#include "common/log_wrapper.h"

namespace aicode {

InputPanel::InputPanel(float x, float y, float width, float height)
    : width_(width), height_(height) {
    panel_ = std::make_unique<UIPanel>(x, y, width, height, PanelStyle::InputField());

    input_text_ = std::make_unique<imgui_widget::InputText>("##Input", "", 1024,
        [this](const std::string& /*text*/) {
            // 文本变化时的回调（可选）
        });
    input_text_->SetEnterReturnsTrue(true);

    send_button_ = std::make_unique<imgui_widget::Button>("发送(Send)", [this]() {
        std::string msg(input_text_->GetText());
        if (!msg.empty()) {
            input_text_->SetText("");
            if (on_submit_) {
                on_submit_(msg);
            }
        }
    });
}

InputPanel::~InputPanel() = default;

void InputPanel::SetPosition(float x, float y) {
    panel_->SetPosition(x, y);
}

void InputPanel::SetSize(float width, float height) {
    panel_->SetSize(width, height);
    width_ = width;
    height_ = height;
}

void InputPanel::Render() const {
    if (!visible_) return;
    panel_->Render();
}

void InputPanel::RenderContent() {
    if (!visible_) return;

    using namespace imgui_widget;

    float panel_x = panel_->GetX();
    float panel_y = panel_->GetY();

    SetImGuiNextWindowPos(panel_x + 5, panel_y + 5);
    SetImGuiNextWindowSize(width_ - 15, height_ - 10);
    SetImGuiNextWindowBgAlpha(0.0f);

    ImGuiBegin("输入框(Input): ", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGuiPushItemWidth(width_ - 90);
    input_text_->Render();
    if (input_text_->IsEnterPressed()) {
        std::string msg(input_text_->GetText());
        if (!msg.empty()) {
            input_text_->SetText("");
            if (on_submit_) {
                on_submit_(msg);
            }
        }
    }
    ImGuiPopItemWidth();

    ImGuiSameLine();
    send_button_->Render();

    ImGuiEnd();
}

std::string InputPanel::GetText() const {
    return input_text_->GetText();
}

void InputPanel::SetText(const std::string& text) {
    input_text_->SetText(text);
}

void InputPanel::SetOnSubmit(SubmitCallback cb) {
    on_submit_ = cb;
}

void InputPanel::SetFocus() {
    // TODO: ImGui 聚焦输入框
}

}  // namespace aicode
