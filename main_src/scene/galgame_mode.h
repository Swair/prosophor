// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>
#include <vector>

#include "media_engine/media_engine.h"
#include "components/ui_types.h"

namespace prosophor {

/// GALGAME 模式场景 - 可操作角色行走 + 对话
class GalgameScene {
public:
    static GalgameScene& Instance();

    void Initialize();
    void Render();
    void Update(float delta_time);

    /// 处理键盘事件（WASD/方向键移动角色）
    void HandleKeyDown(int key_code);

    /// 对话管理
    void AddDialogue(const std::string& speaker, const std::string& text);
    void ClearDialogue();

    /// 设置对话颜色
    void SetSpeakerColor(const std::string& speaker, uint8_t r, uint8_t g, uint8_t b);

private:
    GalgameScene() = default;

    /// 渲染场景背景（教室）
    void RenderBackground();

    /// 渲染角色（像素风格）
    void RenderCharacter();

    /// 渲染对话框
    void RenderDialogueBox();

    /// 绘制像素（放大方块）
    void DrawPixel(float x, float y, int px, int py, const Color& color) const;

    // 角色状态
    float char_x_ = 400.0f;
    float char_y_ = 600.0f;
    float char_target_x_ = 400.0f;
    float char_target_y_ = 600.0f;
    float move_speed_ = 200.0f;  // 像素/秒

    // 角色方向（0=下, 1=左, 2=右, 3=上）
    int char_direction_ = 0;

    // 行走动画
    float anim_timer_ = 0.0f;
    int anim_frame_ = 0;
    bool is_moving_ = false;

    // 对话数据
    struct DialogueLine {
        std::string speaker;
        std::string text;
    };
    std::vector<DialogueLine> dialogues_;
    std::map<std::string, Color> speaker_colors_;

    // 当前对话索引（对话框显示滚动）
    int dialogue_scroll_ = 0;
    static constexpr int kMaxVisibleDialogue = 6;
};

}  // namespace prosophor
