// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <memory>

// Texture 前向声明
class Texture;

namespace aicode {

/// Direction enum (matches pixel-agents)
enum class CharDirection {
    DOWN = 0,
    LEFT = 1,
    RIGHT = 2,
    UP = 3
};

/// Character state enum
enum class CharacterState {
    IDLE = 0,
    WALK = 1,
    TYPE = 2,    // Typing at desk
    READ = 3     // Reading/searching
};

/// SpriteFrame: 精灵图的一帧（纹理裁剪区域）
struct SpriteFrameRect {
    float x, y, w, h;  // 在纹理中的裁剪区域
};

/// CharacterSprites: 一个角色的所有动画帧
struct CharacterSprites {
    // Walk: 4 方向 x 4 帧
    std::array<std::array<SpriteFrameRect, 4>, 4> walk;
    // Typing: 4 方向 x 2 帧
    std::array<std::array<SpriteFrameRect, 2>, 4> typing;
    // Reading: 4 方向 x 2 帧
    std::array<std::array<SpriteFrameRect, 2>, 4> reading;
};

/// Character definition (role-based)
struct CharacterDef {
    std::string id;           // "coder", "reviewer", "architect"
    std::string name;         // Display name
    std::string avatar;       // Emoji or icon
    int palette_index = 0;    // 0-5 for different hair/clothes
    float hue_shift = 0;      // Additional hue rotation
};

/// Runtime character instance
struct CharacterInstance {
    CharacterDef def;
    CharacterState state = CharacterState::IDLE;
    CharDirection direction = CharDirection::DOWN;

    // Position (tile coordinates)
    int tile_col = 0;
    int tile_row = 0;

    // Position (pixel coordinates, for smooth movement)
    float x = 0;
    float y = 0;

    // Animation
    int frame = 0;
    float frame_timer = 0;

    // Pathfinding
    std::vector<std::pair<int, int>> path;
    float move_progress = 0;

    // Activity tracking
    std::string current_tool;
    bool is_active = true;

    // Seat assignment
    std::string seat_id;
    int seat_col = 0;
    int seat_row = 0;

    // Idle timer for random movement
    float idle_timer = 0;
};

/// CharacterSpriteRenderer: loads and renders character sprites
class CharacterSpriteRenderer {
public:
    static CharacterSpriteRenderer& Instance();

    /// Initialize with sprite sheets from assets
    void Initialize();

    /// Get sprites for a palette index
    const CharacterSprites& GetSprites(int palette_index) const;

    /// Get current frame rect for a character
    const SpriteFrameRect* GetCurrentFrameRect(const CharacterInstance& ch) const;

    /// Get texture for a palette index
    Texture* GetTexture(int palette_index) const;

    /// Render character to renderer
    void Render(const CharacterInstance& ch, float x, float y, float scale = 1.0f);

private:
    CharacterSpriteRenderer() = default;

    /// Load sprites from PNG sprite sheet to specific palette
    bool LoadFromPng(const std::string& path, int palette_index);

    // 6 palette variations, each with its own texture
    std::array<std::unique_ptr<Texture>, 6> textures_;
    std::array<CharacterSprites, 6> sprite_rects_;
};

}  // namespace aicode
