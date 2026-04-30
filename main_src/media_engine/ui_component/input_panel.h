// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ui_component/ui_container.h"

// Forward declarations (imgui_widget is a global namespace, avoids including imgui_widget.h)
namespace imgui_widget {
class InputText;
class Button;
}

namespace prosophor {

/// @brief 输入面板 - 可复用的输入区域组件
///
/// 数据流：
///   输入：用户文本输入（键盘输入 / SetText 设置）
///   输出：用户提交时通过 on_submit_ 回调发送文本内容
///
/// 交互方式：
///   1. 用户在输入框中输入文本
///   2. 按 Enter 键或点击"发送"按钮触发提交
///   3. 提交后文本通过 SubmitCallback 回调发送，输入框自动清空
class InputPanel {
public:
    /// @brief 提交回调 - 输入文本被提交时触发
    /// @param text 用户输入的文本内容（非空）
    using SubmitCallback = std::function<void(const std::string&)>;

    /// @brief 构造函数
    /// @param x 左上角 X 坐标
    /// @param y 左上角 Y 坐标
    /// @param width 面板宽度
    /// @param height 面板高度
    InputPanel(float x, float y, float width, float height);
    ~InputPanel();

    /// @brief 设置位置
    /// @param x X 坐标
    /// @param y Y 坐标
    void SetPosition(float x, float y);
    void SetSize(float width, float height);

    /// @brief 渲染背景（SDL 层）
    void Render() const;

    /// @brief 渲染输入框和按钮（ImGui 层）
    void RenderContent();

    /// @brief 获取输入框当前文本
    /// @return 输入框中的文本
    std::string GetText() const;

    /// @brief 设置输入框文本（程序设置 / 清空等）
    /// @param text 要设置的文本
    void SetText(const std::string& text);

    /// @brief 设置提交回调
    /// @param cb 回调函数，接收提交的文本（非空）
    void SetOnSubmit(SubmitCallback cb);

    /// @brief 设置可见性
    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

    /// @brief 聚焦输入框
    void SetFocus();

private:
    std::unique_ptr<UIContainer> container_;
    std::unique_ptr<imgui_widget::InputText> input_text_;
    std::unique_ptr<imgui_widget::Button> send_button_;
    SubmitCallback on_submit_;
    bool visible_ = true;
};

}  // namespace prosophor
