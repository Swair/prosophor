// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "media/colors.h"
#include "scene/character_sprite.h"

namespace aicode {

/// 像素小人渲染器 - 使用 Drawer 绘制像素风格角色，替代 PNG 精灵图
class PixelCharacterRenderer {
public:
    static PixelCharacterRenderer& Instance();

    void Initialize();

    /// 渲染角色（根据方向选择正面/背面/侧面）
    void Render(const CharacterInstance& ch, float x, float y, float scale);

private:
    /// 绘制单个像素（放大后的像素块）
    void DrawPixel(float base_x, float base_y, int px, int py, const Color& color) const;

    /// 绘制正面视角
    void RenderCharacter(float x, float y, int palette_index) const;

    /// 绘制背面视角
    void RenderCharacterBack(float x, float y, int palette_index) const;

    /// 绘制侧面视角
    void RenderCharacterSide(float x, float y, int palette_index, CharDirection dir) const;
};

}  // namespace aicode
