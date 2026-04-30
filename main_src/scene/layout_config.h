// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace prosophor {

/// 布局配置 - 统一管理所有 UI 和场景元素的布局
struct LayoutConfig {
    // 右侧聊天面板区域
    float chat_panel_x_ratio = 0.65f;
    float chat_panel_width_ratio = 0.35f;

    // 底部输入区高度
    float input_area_height = 100.0f;

    // 底部状态栏高度
    float status_bar_height = 30.0f;

    // 获取聊天面板位置
    static float GetChatPanelX(float window_width) {
        return window_width * LayoutConfig{}.chat_panel_x_ratio;
    }
    static float GetChatPanelWidth(float window_width) {
        return window_width * LayoutConfig{}.chat_panel_width_ratio;
    }
    static float GetChatPanelY() { return 0.0f; }
    static float GetChatPanelHeight(float window_height, float input_height, float status_height) {
        return window_height - input_height - status_height - 10.0f;
    }

    // 获取输入面板 Y 位置
    static float GetInputPanelY(float window_height, float status_height) {
        return window_height - status_height - LayoutConfig{}.input_area_height;
    }

    // 获取状态栏 Y 位置
    static float GetStatusBarY(float window_height, float status_height) {
        return window_height - status_height;
    }
};

}  // namespace prosophor
