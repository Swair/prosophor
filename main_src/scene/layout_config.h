// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace aicode {

/// 布局配置 - 统一管理所有 UI 和场景元素的布局
struct LayoutConfig {
    // 右侧聊天面板区域（占屏幕 70% 宽度）
    float chat_panel_x_ratio = 0.70f;
    float chat_panel_width_ratio = 0.30f;

    // 底部输入区高度
    float input_area_height = 100.0f;

    // 底部状态栏高度
    float status_bar_height = 30.0f;

    // 办公室场景区域（左侧 70%）
    float office_x = 0.0f;
    float office_y = 0.0f;
    float office_width_ratio = 0.70f;
    float office_height_ratio = 1.0f;

    // 办公室网格配置
    float tile_size = 32.0f;  // 每个 tile 的像素大小

    // 角色位置（办公室场景内）
    float character_x = 200.0f;
    float character_y = 400.0f;

    // 办公室内部元素布局（相对于办公室区域）
    struct Office {
        // 墙壁厚度
        float wall_thickness = 50.0f;
        // 门位置（底部居中）
        float door_width = 100.0f;
        float door_height = 50.0f;
        // 窗户位置
        float window1_x_ratio = 0.15f;
        float window1_width_ratio = 0.14f;
        float window2_y_ratio = 0.2f;
        float window2_height_ratio = 0.28f;
        // 桌子位置（居中）
        float desk_x_ratio = 0.43f;
        float desk_y_ratio = 0.39f;
        float desk_width_ratio = 0.23f;
        float desk_height_ratio = 0.22f;
        // 电脑位置（桌子上）
        float computer_x_ratio = 0.5f;
        float computer_y_ratio = 0.42f;
        float computer_width_ratio = 0.08f;
        float computer_height_ratio = 0.1f;
        // 椅子位置（桌子前）
        float chair_x_ratio = 0.45f;
        float chair_y_ratio = 0.65f;
    };

    // 获取聊天面板位置（根据窗口尺寸计算）
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

    // 获取办公室场景位置
    static float GetOfficeX() { return 0.0f; }
    static float GetOfficeY() { return 0.0f; }
    static float GetOfficeWidth(float window_width) {
        return window_width * LayoutConfig{}.office_width_ratio;
    }
    static float GetOfficeHeight(float window_height) {
        return window_height * LayoutConfig{}.office_height_ratio;
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

}  // namespace aicode
