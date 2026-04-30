// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file input_panel.cc
///
/// 数据流说明：
///   输入来源：
///     - 用户键盘输入（ImGui InputText 控件）
///     - 程序调用 SetText() 设置
///   输出方式：
///     - 用户按 Enter 或点击"发送"按钮时触发 on_submit_ 回调
///     - 回调参数为用户输入的文本内容（自动过滤空字符串）
///     - 提交后输入框自动清空

#include "ui_component/input_panel.h"
#include "ui_component/ui_container.h"
#include "ui_component/ui_panel.h"
#include "media/imgui_widget.h"
#include "log_wrapper.h"

namespace prosophor {

InputPanel::InputPanel(float x, float y, float width, float height) {
    // 创建容器面板，使用输入区域风格
    container_ = std::make_unique<UIContainer>(x, y, width, height, PanelStyle::InputField());

    // 创建输入框控件
    input_text_ = std::make_unique<imgui_widget::InputText>("##Input", "", 1024,
        [this](const std::string& /*text*/) {
            // 文本变化时的回调（可选）
        });
    input_text_->SetEnterReturnsTrue(true);

    // 创建发送按钮：点击输入框文本 → 触发回调 → 清空输入框
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
    container_->SetPosition(x, y);
}

void InputPanel::SetSize(float width, float height) {
    container_->SetSize(width, height);
}

void InputPanel::Render() const {
    if (!visible_) return;
    container_->Render();
}

/// @brief 渲染输入框和按钮
///
/// 在 RenderContent 中处理提交逻辑（而非构造函数中的按钮回调），
/// 确保每次渲染都能响应 Enter 键和按钮点击。
void InputPanel::RenderContent() {
    if (!visible_) return;

    container_->SetContentCallback([this](float /*cx*/, float /*cy*/, float content_width, float /*ch*/) {
        using namespace imgui_widget;

        // 输入框占据剩余宽度（预留按钮 85px）
        ImGuiPushItemWidth(content_width - 85);
        input_text_->Render();

        // Enter 键提交：获取文本 → 触发回调 → 清空输入框
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
    });

    container_->RenderContent("InputPanel");
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

}  // namespace prosophor
