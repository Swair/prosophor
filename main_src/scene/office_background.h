// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"
#include "scene/office_character_manager.h"
#include <string>
#include <memory>

class Texture;

namespace aicode {

/// Office background renderer
/// Draws a simple office room background with desk, chair, and walls
class OfficeBackground : public Noncopyable {
 public:
    static OfficeBackground& GetInstance();

    /// Initialize background resources
    void Initialize();

    /// Render the office background
    void Render();

    /// Get tile columns
    int GetTileCols() const { return tile_cols_; }

    /// Get tile rows
    int GetTileRows() const { return tile_rows_; }

 private:
    OfficeBackground() = default;

    /// Draw walls
    void DrawWalls();

    /// Draw floor
    void DrawFloor();

    /// Draw desk
    void DrawDesk();

    /// Draw chair
    void DrawChair();

    /// Draw window
    void DrawWindow();

    /// Draw computer monitor
    void DrawComputer();

    /// Draw door
    void DrawDoor();

    /// Draw person (little man) - deprecated, use OfficeCharacterManager
    void DrawPerson();

    /// Draw plant decoration
    void DrawPlant();

    int tile_cols_ = 40;
    int tile_rows_ = 22;

    // Office area dimensions (cached from LayoutConfig)
    float office_x_ = 0.0f;
    float office_y_ = 0.0f;
    float office_width_ = 896.0f;   // 1280 * 0.7
    float office_height_ = 720.0f;  // 720 * 1.0
};

}  // namespace aicode
