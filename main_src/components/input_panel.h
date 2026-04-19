// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ui_panel.h"
#include <string>
#include <functional>
#include <memory>

#include "media/imgui_widget.h"

// ============================================================================
// 输入面板组件 - 聚合背景、输入框、发送按钮
// ============================================================================
namespace aicode {

/// 输入面板 - 可复用的输入区域组件
class InputPanel {
public:
    using SubmitCallback = std::function<void(const std::string&)>;

    InputPanel(float x, float y, float width, float height);
    ~InputPanel();

    // 设置位置尺寸
    void SetPosition(float x, float y);
    void SetSize(float width, float height);

    // 渲染背景（SDL 层）
    void Render() const;

    // 渲染输入框和按钮（ImGui 层）
    void RenderContent();

    // 获取/设置文本
    std::string GetText() const;
    void SetText(const std::string& text);

    // 回调
    void SetOnSubmit(SubmitCallback cb);

    // 可见性
    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

    // 聚焦
    void SetFocus();

private:
    std::unique_ptr<UIPanel> panel_;
    std::unique_ptr<imgui_widget::InputText> input_text_;
    std::unique_ptr<imgui_widget::Button> send_button_;
    SubmitCallback on_submit_;
    bool visible_ = true;
    float width_ = 0, height_ = 0;
};

}  // namespace aicode
