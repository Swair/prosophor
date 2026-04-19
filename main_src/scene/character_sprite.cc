// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene/character_sprite.h"
#include "media_core.h"
#include "drawer.h"
#include "media/colors.h"
#include "media/texture.h"
#include "common/log_wrapper.h"
#include <cmath>
#include <algorithm>
#include <fstream>

namespace aicode {

// PNG 布局：112x96 = 7 列 x 3 行，每帧 16x32 像素
constexpr int FRAME_W = 16;
constexpr int FRAME_H = 32;
constexpr int FRAMES_PER_ROW = 7;
constexpr int PNG_W = 112;
constexpr int PNG_H = 96;

CharacterSpriteRenderer& CharacterSpriteRenderer::Instance() {
    static CharacterSpriteRenderer instance;
    return instance;
}

void CharacterSpriteRenderer::Initialize() {
    LOG_INFO("CharacterSpriteRenderer initializing...");

    // 从 assets 加载 PNG 精灵图
    std::vector<std::string> char_paths = {
        "assets/characters/char_0.png",
        "assets/characters/char_1.png",
        "assets/characters/char_2.png",
        "assets/characters/char_3.png",
        "assets/characters/char_4.png",
        "assets/characters/char_5.png"
    };

    int loaded_count = 0;
    for (size_t i = 0; i < char_paths.size(); i++) {
        if (LoadFromPng(char_paths[i], static_cast<int>(i))) {
            loaded_count++;
            LOG_INFO("Loaded character {} from {}", i, char_paths[i]);
        }
    }

    if (loaded_count == 0) {
        LOG_WARN("No PNGs loaded, characters will not be rendered.");
    } else {
        LOG_INFO("CharacterSpriteRenderer initialized with {} PNG sprites.", loaded_count);
    }
}

bool CharacterSpriteRenderer::LoadFromPng(const std::string& path, int palette_index) {
    textures_[palette_index] = std::make_unique<Texture>(path);
    auto& tex = textures_[palette_index];

    if (tex->GetOriginWidth() != PNG_W || tex->GetOriginHeight() != PNG_H) {
        LOG_WARN("LoadFromPng: Unexpected size {}x{}, expected {}x{}",
                 tex->GetOriginWidth(), tex->GetOriginHeight(), PNG_W, PNG_H);
        textures_[palette_index] = nullptr;
        return false;
    }

    auto& chars = sprite_rects_[palette_index];

    // 辅助函数：获取帧的裁剪矩形
    auto getFrameRect = [&](int frame_idx) -> SpriteFrameRect {
        if (frame_idx >= 21) {
            return {0, 0, static_cast<float>(FRAME_W), static_cast<float>(FRAME_H)};
        }
        int row = frame_idx / FRAMES_PER_ROW;
        int col = frame_idx % FRAMES_PER_ROW;
        return {
            static_cast<float>(col * FRAME_W),
            static_cast<float>(row * FRAME_H),
            static_cast<float>(FRAME_W),
            static_cast<float>(FRAME_H)
        };
    };

    // Walk 动画 (4 帧)
    for (int frame = 0; frame < 4; frame++) {
        chars.walk[static_cast<int>(CharDirection::DOWN)][frame] = getFrameRect(frame);
        chars.walk[static_cast<int>(CharDirection::UP)][frame] = getFrameRect(4 + frame);
        chars.walk[static_cast<int>(CharDirection::RIGHT)][frame] = getFrameRect(8 + frame);
        if (12 + frame < 21) {
            chars.walk[static_cast<int>(CharDirection::LEFT)][frame] = getFrameRect(12 + frame);
        } else {
            chars.walk[static_cast<int>(CharDirection::LEFT)][frame] =
                chars.walk[static_cast<int>(CharDirection::RIGHT)][frame];
        }
    }

    // Typing 动画 (2 帧)
    for (int frame = 0; frame < 2; frame++) {
        chars.typing[static_cast<int>(CharDirection::DOWN)][frame] = getFrameRect(14 + frame);
        chars.typing[static_cast<int>(CharDirection::UP)][frame] = getFrameRect(14 + frame);
        chars.typing[static_cast<int>(CharDirection::RIGHT)][frame] =
            chars.walk[static_cast<int>(CharDirection::RIGHT)][0];
        chars.typing[static_cast<int>(CharDirection::LEFT)][frame] =
            chars.walk[static_cast<int>(CharDirection::LEFT)][0];
    }

    // Reading 动画 (2 帧)
    for (int frame = 0; frame < 2; frame++) {
        chars.reading[static_cast<int>(CharDirection::DOWN)][frame] = getFrameRect(16 + frame);
        chars.reading[static_cast<int>(CharDirection::UP)][frame] = getFrameRect(16 + frame);
        for (int dir = 2; dir < 4; dir++) {
            chars.reading[dir][frame] = chars.typing[dir][frame];
        }
    }

    return true;
}

const CharacterSprites& CharacterSpriteRenderer::GetSprites(int palette_index) const {
    return sprite_rects_[palette_index % 6];
}

Texture* CharacterSpriteRenderer::GetTexture(int palette_index) const {
    return textures_[palette_index].get();
}

const SpriteFrameRect* CharacterSpriteRenderer::GetCurrentFrameRect(const CharacterInstance& ch) const {
    const auto& chars = sprite_rects_[ch.def.palette_index % 6];

    switch (ch.state) {
        case CharacterState::WALK:
            return &chars.walk[static_cast<int>(ch.direction)][ch.frame % 4];
        case CharacterState::TYPE:
            return &chars.typing[static_cast<int>(ch.direction)][ch.frame % 2];
        case CharacterState::READ:
            return &chars.reading[static_cast<int>(ch.direction)][ch.frame % 2];
        case CharacterState::IDLE:
        default:
            return &chars.walk[static_cast<int>(ch.direction)][0];
    }
}

void CharacterSpriteRenderer::Render(const CharacterInstance& ch, float x, float y, float scale) {
    const Texture* tex = GetTexture(ch.def.palette_index % 6);
    if (!tex) return;

    const SpriteFrameRect* frame = GetCurrentFrameRect(ch);
    if (!frame) return;

    // 计算渲染尺寸
    float render_w = frame->w * scale;
    float render_h = frame->h * scale;

    // 是否需要水平翻转（LEFT 方向）
    bool flip_h = (ch.direction == CharDirection::LEFT);

    // 渲染纹理
    tex->RenderTexture(frame->x, frame->y, frame->w, frame->h, x, y, render_w, render_h, flip_h, false);
}

}  // namespace aicode
