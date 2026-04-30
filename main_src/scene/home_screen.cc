// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/home_screen.h"
#include "media_engine/media_engine.h"
#include "common/log_wrapper.h"

#include <cmath>

using namespace imgui_widget;

namespace prosophor {

HomeScreen& HomeScreen::GetInstance() {
    static HomeScreen instance;
    return instance;
}

void HomeScreen::Initialize() {
    LOG_INFO("HomeScreen initialized.");
}

void HomeScreen::Render() {
    int win_w = MediaCore::Instance().GetWindowWidth();
    int win_h = MediaCore::Instance().GetWindowHeight();

    // 深色渐变背景
    Color bg_top(18, 18, 28, 255);
    Color bg_bot(10, 10, 18, 255);
    int steps = 8;
    for (int i = 0; i < steps; i++) {
        float t = static_cast<float>(i) / steps;
        uint8_t r = static_cast<uint8_t>(bg_top.r + (bg_bot.r - bg_top.r) * t);
        uint8_t g = static_cast<uint8_t>(bg_top.g + (bg_bot.g - bg_top.g) * t);
        uint8_t b = static_cast<uint8_t>(bg_top.b + (bg_bot.b - bg_top.b) * t);
        float y = win_h * t;
        float h = win_h / steps + 1;
        ::Drawer::Instance().DrawFillRect(0, y, static_cast<float>(win_w), h, Color(r, g, b, 255));
    }

    float line_alpha = 0.5f + 0.3f * std::sin(animation_time_ * 2.0f);
    uint8_t la = static_cast<uint8_t>(line_alpha * 255);
    ::Drawer::Instance().DrawFillRect(
        win_w * 0.3f, win_h * 0.14f, win_w * 0.4f, 1.5f,
        Color(120, 140, 255, la));

    // === 单窗口，零 padding ===
    SetImGuiNextWindowPos(0, 0);
    SetImGuiNextWindowSize(static_cast<float>(win_w), static_cast<float>(win_h));
    SetImGuiNextWindowBgAlpha(0.0f);

    ImGuiPushStyleVar_WindowPadding(0, 0);
    ImGuiPushStyleVar_FramePadding(0, 0);
    ImGuiPushStyleVar_ItemSpacing(0, 0);
    ImGuiPushStyleVar_ItemInnerSpacing(0, 0);

    ImGuiBegin("HomeScreen", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoMove);

    // 标题
    float title_y = win_h * 0.10f;
    ImGuiSetCursorPos(win_w * 0.5f - 120, title_y);
    ImGuiTextColored(Color(200, 210, 255, 255), "PROSOPHOR");

    ImGuiSetCursorPos(win_w * 0.5f - 90, title_y + 30);
    ImGuiTextColored(Color(140, 150, 180, 255), "Choose Your Experience");

    ImGuiSetCursorPos(win_w * 0.5f - 150, title_y + 56);
    ImGuiTextColored(Color(90, 100, 140, 255),
        "──────────────────────────────────");

    float btn_w = 220.0f;
    float btn_h = 130.0f;
    float gap = 20.0f;
    float total_w = btn_w * 3 + gap * 2;
    float start_x = (win_w - total_w) * 0.5f;
    float btn_y = win_h * 0.28f;

    struct ModeInfo {
        UIMode mode;
        const char* icon;
        const char* title;
        const char* desc;
        Color icon_clr;
        Color border_clr;
    };

    ModeInfo modes[] = {
        {UIMode::GALGAME,  "◆", "GALGAME",  "Story-driven interactive narrative",
         Color(255, 120, 198, 220), Color(255, 120, 198, 100)},
        {UIMode::VIRTUAL_HUMAN, "◇", "虚拟人", "Virtual character interactive dialogue",
         Color(100, 160, 255, 220), Color(100, 160, 255, 100)},
        {UIMode::TERMINAL, "◈", "TERMINAL", "Classic terminal CLI experience",
         Color(160, 200, 140, 220), Color(160, 200, 140, 100)},
    };

    for (int i = 0; i < 3; i++) {
        float bx = start_x + i * (btn_w + gap);
        float ly = btn_y;

        // 按钮背景（圆角矩形，带 alpha）
        uint32_t bg_color = ColorToIM_COL32(Color(20, 20, 35, 220));
        DrawFilledRoundRect(bx, ly, btn_w, btn_h, 4.0f, bg_color);

        // 按钮边框
        uint32_t border_color = ColorToIM_COL32(modes[i].border_clr);
        DrawRoundRectOutline(bx, ly, btn_w, btn_h, 4.0f, border_color, 1.5f);

        // 文字
        ImGuiSetCursorPos(bx + btn_w * 0.5f - 14, ly + 8);
        ImGuiTextColored(modes[i].icon_clr, modes[i].icon);

        ImGuiSetCursorPos(bx + btn_w * 0.5f - 45, ly + 36);
        ImGuiTextColored(Color(242, 242, 250, 255), modes[i].title);

        ImGuiSetCursorPos(bx + 14, ly + 60);
        ImGuiPushTextWrapPos(bx + btn_w - 14);
        ImGuiTextColored(Color(150, 150, 170, 255), modes[i].desc);
        ImGuiPopTextWrapPos();

        // 可点击区域
        ImGuiSetCursorPos(bx, ly);
        if (ImGuiInvisibleButton(("ib" + std::to_string(i)).c_str(), btn_w, btn_h)) {
            LOG_INFO("Mode button {} clicked, switching to mode {}", i, static_cast<int>(modes[i].mode));
            if (on_mode_select_) {
                on_mode_select_(modes[i].mode);
            }
        }
    }

    // 底部
    ImGuiSetCursorPos(win_w * 0.5f - 110, win_h * 0.76f);
    ImGuiTextColored(Color(85, 85, 112, 255), "Prosophor AI Agent Platform");

    ImGuiEnd();
    ImGuiPopStyleVar(4);

    animation_time_ += MediaCore::Instance().GetDeltaTimeS();
}

void HomeScreen::SetOnModeSelect(ModeSelectCallback cb) {
    on_mode_select_ = cb;
}

}  // namespace prosophor
