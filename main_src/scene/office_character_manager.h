// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "scene/character_sprite.h"
#include "scene/agent_state_observer.h"
#include <array>

namespace aicode {

/// OfficeCharacterManager: manages 3 roles (coder, reviewer, architect) in office scene
/// Reference: pixel-agents character system with state machine + pathfinding
class OfficeCharacterManager {
public:
    static OfficeCharacterManager& Instance();

    /// Initialize with office tile map
    void Initialize(int tile_cols, int tile_rows);

    /// Update all characters (state machine + pathfinding + animation)
    void Update(float dt);

    /// Render all characters
    void Render();

    /// Set character activity and tool (called by Agent state observer)
    void SetCharacterActivity(const std::string& role_id, bool is_active,
                              const std::string& current_tool = "");

    /// Assign character to seat (desk position)
    void AssignSeat(const std::string& role_id, int seat_col, int seat_row, CharDirection facing);

    /// Get character by role ID
    CharacterInstance* GetCharacter(const std::string& role_id);

    /// Trigger state change (for debugging)
    void SetCharacterState(const std::string& role_id, CharacterState state);

private:
    OfficeCharacterManager() = default;

    /// Initialize 3 default characters (coder, reviewer, architect)
    void InitializeDefaultCharacters();

    /// Update single character state machine
    void UpdateCharacter(CharacterInstance& ch, float dt);

    /// Pathfinding: find walkable path from A to B (simple BFS)
    std::vector<std::pair<int, int>> FindPath(int from_col, int from_row,
                                               int to_col, int to_row);

    /// Check if tile is walkable
    bool IsWalkable(int col, int row) const;

    /// Get tile center in pixels
    std::pair<float, float> TileCenter(int col, int row) const;

    // Characters indexed by role
    std::array<CharacterInstance, 3> characters_;

    // Tile map reference
    int tile_cols_ = 0;
    int tile_row_ = 0;

    // Walkable tiles (floor, not walls/furniture)
    std::vector<std::pair<int, int>> walkable_tiles_;

    // Constants
    static constexpr float WALK_SPEED_PX_PER_SEC = 80.0f;
    static constexpr float WALK_FRAME_DURATION = 0.15f;
    static constexpr float TYPE_FRAME_DURATION = 0.3f;
    static constexpr float TILE_SIZE = 32.0f;
};

}  // namespace aicode
