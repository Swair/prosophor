// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/galgame_mode.h"
#include <algorithm>

#include "common/log_wrapper.h"
#include "media_engine/media_engine.h"

namespace prosophor {

GalgameScene& GalgameScene::Instance() {
    static GalgameScene instance;
    return instance;
}

void GalgameScene::Initialize() {
    LOG_INFO("GalgameScene initialized.");
    // 默认角色颜色 - 紫色西装老师风格
    speaker_colors_["老师"] = Color(180, 100, 220, 255);
    speaker_colors_["系统"] = Color(100, 100, 100, 255);
    AddDialogue("系统", "欢迎来到 GALGAME 模式！");
    AddDialogue("系统", "使用 WASD 或方向键移动角色。");
    AddDialogue("老师", "你好呀，我是你的 AI 老师！");
}

void GalgameScene::Update(float delta_time) {
    // 角色平滑移动
    float dx = char_target_x_ - char_x_;
    float dy = char_target_y_ - char_y_;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > 1.0f) {
        float step = move_speed_ * delta_time;
        if (step > dist) step = dist;
        char_x_ += (dx / dist) * step;
        char_y_ += (dy / dist) * step;
        is_moving_ = true;

        anim_timer_ += delta_time;
        if (anim_timer_ > 0.15f) {
            anim_timer_ = 0.0f;
            anim_frame_ = (anim_frame_ + 1) % 4;
        }
    } else {
        char_x_ = char_target_x_;
        char_y_ = char_target_y_;
        is_moving_ = false;
        anim_frame_ = 0;
    }
}

void GalgameScene::Render() {
    int win_w = MediaCore::Instance().GetWindowWidth();
    int win_h = MediaCore::Instance().GetWindowHeight();
    float scene_w = win_w * 0.65f;

    // 清空场景区域
    ::Drawer::Instance().DrawFillRect(0, 0, static_cast<float>(scene_w),
                                       static_cast<float>(win_h), Color(30, 30, 40, 255));

    RenderBackground();
    RenderCharacter();
    RenderDialogueBox();
}

// ============================================================================
// 场景背景 - 教室风格
// ============================================================================
void GalgameScene::RenderBackground() {
    int win_w = MediaCore::Instance().GetWindowWidth();
    int win_h = MediaCore::Instance().GetWindowHeight();
    float scene_w = win_w * 0.65f;

    float floor_y = win_h * 0.5f;
    float wall_h = floor_y;

    // 墙壁（浅蓝色）
    ::Drawer::Instance().DrawFillRect(0, 0, scene_w, wall_h, Color(200, 210, 230, 255));

    // 地板（木纹色）
    ::Drawer::Instance().DrawFillRect(0, floor_y, scene_w, win_h - floor_y, Color(160, 130, 90, 255));

    // 地板格子线
    Color line(140, 110, 70, 80);
    for (float x = 0; x < scene_w; x += 80) {
        ::Drawer::Instance().DrawLine(x, floor_y, x, static_cast<float>(win_h), line);
    }
    for (float y = floor_y; y < win_h; y += 40) {
        ::Drawer::Instance().DrawLine(0, y, scene_w, y, line);
    }

    // 黑板（墙上）
    float board_x = scene_w * 0.2f;
    float board_w = scene_w * 0.6f;
    float board_y = win_h * 0.08f;
    float board_h = win_h * 0.28f;

    // 木框
    Color frame(139, 105, 48, 255);
    ::Drawer::Instance().DrawFillRect(board_x - 6, board_y - 6, board_w + 12, 6, frame);
    ::Drawer::Instance().DrawFillRect(board_x - 6, board_y + board_h, board_w + 12, 6, frame);
    ::Drawer::Instance().DrawFillRect(board_x - 6, board_y, 6, board_h, frame);
    ::Drawer::Instance().DrawFillRect(board_x + board_w, board_y, 6, board_h, frame);

    // 黑板面
    ::Drawer::Instance().DrawFillRect(board_x, board_y, board_w, board_h, Color(40, 70, 40, 255));

    // 粉笔字（白色小矩形模拟）
    Color chalk(255, 255, 240, 100);
    for (int i = 0; i < 15; i++) {
        float ch_x = board_x + 20 + (i % 5) * 100;
        float ch_y = board_y + 15 + (i / 5) * 30;
        ::Drawer::Instance().DrawFillRect(ch_x, ch_y, 60 + std::rand() % 30, 3, chalk);
    }

    // 窗（墙上）
    float win1_x = 20;
    float win1_y = win_h * 0.1f;
    float win1_w = 80;
    float win1_h = 100;
    Color window_frame(200, 190, 170, 255);
    ::Drawer::Instance().DrawFillRect(win1_x, win1_y, win1_w, win1_h, Color(135, 196, 230, 255));
    ::Drawer::Instance().DrawRect(win1_x, win1_y, win1_w, win1_h, window_frame);
    ::Drawer::Instance().DrawLine(win1_x + win1_w / 2, win1_y, win1_x + win1_w / 2, win1_y + win1_h, window_frame);
    ::Drawer::Instance().DrawLine(win1_x, win1_y + win1_h / 2, win1_x + win1_w, win1_y + win1_h / 2, window_frame);

    // 右侧窗
    float win2_x = scene_w - 100;
    ::Drawer::Instance().DrawFillRect(win2_x, win1_y, win1_w, win1_h, Color(135, 196, 230, 255));
    ::Drawer::Instance().DrawRect(win2_x, win1_y, win1_w, win1_h, window_frame);
    ::Drawer::Instance().DrawLine(win2_x + win1_w / 2, win1_y, win2_x + win1_w / 2, win1_y + win1_h, window_frame);
    ::Drawer::Instance().DrawLine(win2_x, win1_y + win1_h / 2, win2_x + win1_w, win1_y + win1_h / 2, window_frame);

    // 讲台（地板区域）
    float desk_x = scene_w * 0.35f;
    float desk_y = floor_y + 60;
    float desk_w = scene_w * 0.3f;
    float desk_h = 40;
    Color desk(139, 90, 43, 255);
    ::Drawer::Instance().DrawFillRect(desk_x, desk_y, desk_w, desk_h, desk);
    ::Drawer::Instance().DrawRect(desk_x, desk_y, desk_w, desk_h, Color(100, 60, 20, 255));

    // 讲台上的粉笔盒
    ::Drawer::Instance().DrawFillRect(desk_x + desk_w - 40, desk_y - 10, 20, 10, Color(255, 255, 255, 255));

    // 桌椅（学生座位 - 多个小桌子）
    Color student_desk(180, 140, 90, 255);
    Color student_chair(160, 120, 70, 255);
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 3; row++) {
            float dx = 60 + col * (scene_w * 0.14f);
            float dy = floor_y + 130 + row * 120;
            ::Drawer::Instance().DrawFillRect(dx, dy, 70, 30, student_desk);
            ::Drawer::Instance().DrawRect(dx, dy, 70, 30, Color(140, 100, 50, 255));
            // 椅子
            ::Drawer::Instance().DrawFillRect(dx + 25, dy + 35, 20, 20, student_chair);
        }
    }

    // 分割线（场景/面板）
    ::Drawer::Instance().DrawFillRect(scene_w - 2, 0, 2, static_cast<float>(win_h),
                                       Color(80, 80, 100, 255));
}

// ============================================================================
// 角色渲染（像素风格 - 紫色老师小人）
// ============================================================================
void GalgameScene::DrawPixel(float x, float y, int px, int py, const Color& color) const {
    if (px < 0 || px >= 16 || py < 0 || py >= 32) return;
    float dx = x + px * 3.0f;  // 放大3倍
    float dy = y + py * 3.0f;
    ::Drawer::Instance().DrawFillRect(dx, dy, 3.0f, 3.0f, color);
}

void GalgameScene::RenderCharacter() {
    // 像素颜色
    Color hair(90, 40, 120, 255);       // 紫色头发
    Color skin(255, 224, 192, 255);      // 皮肤
    Color suit(50, 30, 70, 255);         // 深紫西装
    Color shoe(40, 30, 50, 255);         // 鞋子
    Color eye(20, 10, 40, 255);          // 眼睛
    Color mouth(200, 100, 100, 255);     // 嘴巴
    Color scarf(100, 150, 255, 255);     // 领结（随状态）
    Color sock(30, 30, 40, 255);         // 黑丝袜
    Color skin2(255, 224, 192, 255);     // 皮肤备用

    float x = char_x_;
    float y = char_y_;

    // 行走动画：腿部摆动
    int leg_offset = 0;
    if (is_moving_) {
        leg_offset = (anim_frame_ % 2) == 0 ? 1 : -1;
    }

    // 头部 (y=0~7)
    // 头发
    for (int py = 0; py <= 2; py++)
        for (int px = 3; px <= 12; px++)
            DrawPixel(x, y, px, py, hair);
    // 前发
    for (int py = 3; py <= 5; py++)
        for (int px = 4; px <= 11; px++)
            DrawPixel(x, y, px, py, hair);
    // 脸
    for (int py = 3; py <= 7; py++)
        for (int px = 5; px <= 10; px++)
            DrawPixel(x, y, px, py, skin);
    // 眼睛
    if (char_direction_ == 0 || char_direction_ == 3) {
        DrawPixel(x, y, 6, 5, eye);
        DrawPixel(x, y, 9, 5, eye);
    }
    // 嘴巴
    DrawPixel(x, y, 7, 7, mouth);
    DrawPixel(x, y, 8, 7, mouth);

    // 身体 (y=8~15)
    for (int py = 8; py <= 15; py++)
        for (int px = 4; px <= 11; px++)
            DrawPixel(x, y, px, py, suit);

    // 领结
    DrawPixel(x, y, 7, 9, scarf);
    DrawPixel(x, y, 8, 9, scarf);
    DrawPixel(x, y, 7, 10, scarf);
    DrawPixel(x, y, 8, 10, scarf);

    // 手臂 (y=8~13)
    for (int py = 8; py <= 13; py++) {
        DrawPixel(x, y, 2, py, suit);
        DrawPixel(x, y, 3, py, suit);
        DrawPixel(x, y, 12, py, suit);
        DrawPixel(x, y, 13, py, suit);
    }
    // 手
    DrawPixel(x, y, 2, 14, skin);
    DrawPixel(x, y, 3, 14, skin);
    DrawPixel(x, y, 12, 14, skin);
    DrawPixel(x, y, 13, 14, skin);

    // 腿部 (y=16~23)
    for (int py = 16; py <= 23; py++) {
        // 左腿
        for (int px = 4; px <= 7; px++) {
            int ox = px + (is_moving_ ? leg_offset : 0);
            DrawPixel(x, y, ox, py, skin);
        }
        // 右腿
        for (int px = 8; px <= 11; px++) {
            int ox = px + (is_moving_ ? -leg_offset : 0);
            DrawPixel(x, y, ox, py, skin);
        }
    }

    // 袜子和鞋子 (y=24~31)
    for (int py = 24; py <= 29; py++) {
        for (int px = 4; px <= 7; px++) {
            int ox = px + (is_moving_ ? leg_offset : 0);
            DrawPixel(x, y, ox, py, sock);
        }
        for (int px = 8; px <= 11; px++) {
            int ox = px + (is_moving_ ? -leg_offset : 0);
            DrawPixel(x, y, ox, py, sock);
        }
    }
    for (int py = 30; py <= 31; py++) {
        for (int px = 4; px <= 7; px++) {
            int ox = px + (is_moving_ ? leg_offset : 0);
            DrawPixel(x, y, ox, py, shoe);
        }
        for (int px = 8; px <= 11; px++) {
            int ox = px + (is_moving_ ? -leg_offset : 0);
            DrawPixel(x, y, ox, py, shoe);
        }
    }
}

// ============================================================================
// 对话框（教室场景底部）
// ============================================================================
void GalgameScene::RenderDialogueBox() {
    int win_w = MediaCore::Instance().GetWindowWidth();
    int win_h = MediaCore::Instance().GetWindowHeight();
    float scene_w = win_w * 0.65f;

    float box_x = 10;
    float box_y = win_h - 140;
    float box_w = scene_w - 20;
    float box_h = 130;

    // 对话框背景
    ::Drawer::Instance().DrawFillRoundRect(box_x, box_y, box_w, box_h, 8.0f,
                                            Color(20, 20, 35, 220));
    ::Drawer::Instance().DrawRoundRect(box_x, box_y, box_w, box_h, 8.0f,
                                        Color(80, 80, 120, 150));

    // 显示可见的对话
    int start = dialogue_scroll_;
    if (start + kMaxVisibleDialogue > static_cast<int>(dialogues_.size())) {
        start = std::max(0, static_cast<int>(dialogues_.size()) - kMaxVisibleDialogue);
    }

    float name_x = box_x + 10;
    float cur_x = box_x + 60;

    for (int i = start; i < start + kMaxVisibleDialogue && i < static_cast<int>(dialogues_.size()); i++) {
        auto& line = dialogues_[i];

        // 说话者名字
        auto it = speaker_colors_.find(line.speaker);
        Color name_color = (it != speaker_colors_.end()) ? it->second : Color(200, 200, 200, 255);
        imgui_widget::ImGuiSetCursorScreenPos(name_x, box_y + 14 + (i - start) * 18);
        imgui_widget::ImGuiTextColored(name_color, line.speaker.c_str());

        // 对话内容
        imgui_widget::ImGuiSetCursorScreenPos(cur_x, box_y + 14 + (i - start) * 18);
        imgui_widget::ImGuiTextUnformatted(line.text.c_str());
    }
}

// ============================================================================
// 键盘操作
// ============================================================================
void GalgameScene::HandleKeyDown(int key_code) {
    float step = 30.0f;
    int win_w = MediaCore::Instance().GetWindowWidth();
    int win_h = MediaCore::Instance().GetWindowHeight();
    float scene_w = win_w * 0.65f;

    bool moved = false;
    switch (key_code) {
        case 'W': case 'w': case 273:  // W / UP arrow
            char_target_y_ -= step;
            char_direction_ = 3;
            moved = true;
            break;
        case 'S': case 's': case 274:  // S / DOWN arrow
            char_target_y_ += step;
            char_direction_ = 0;
            moved = true;
            break;
        case 'A': case 'a': case 276:  // A / LEFT arrow
            char_target_x_ -= step;
            char_direction_ = 1;
            moved = true;
            break;
        case 'D': case 'd': case 275:  // D / RIGHT arrow
            char_target_x_ += step;
            char_direction_ = 2;
            moved = true;
            break;
    }

    if (moved) {
        // 限制在场景区域内
        char_target_x_ = std::clamp(char_target_x_, 30.0f, scene_w - 50.0f);
        char_target_y_ = std::clamp(char_target_y_, win_h * 0.5f, win_h - 160.0f);
    }
}

// ============================================================================
// 对话管理
// ============================================================================
void GalgameScene::AddDialogue(const std::string& speaker, const std::string& text) {
    dialogues_.push_back({speaker, text});
    dialogue_scroll_ = std::max(0, static_cast<int>(dialogues_.size()) - kMaxVisibleDialogue);
}

void GalgameScene::ClearDialogue() {
    dialogues_.clear();
    dialogue_scroll_ = 0;
}

void GalgameScene::SetSpeakerColor(const std::string& speaker, uint8_t r, uint8_t g, uint8_t b) {
    speaker_colors_[speaker] = Color(r, g, b, 255);
}

}  // namespace prosophor
