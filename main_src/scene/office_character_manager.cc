// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene/office_character_manager.h"
#include "scene/character_sprite.h"
#include "scene/layout_config.h"
#include "common/log_wrapper.h"
#include <queue>
#include <set>
#include <random>
#include <algorithm>
#include <map>

namespace aicode {

OfficeCharacterManager& OfficeCharacterManager::Instance() {
    static OfficeCharacterManager instance;
    return instance;
}

void OfficeCharacterManager::Initialize(int tile_cols, int tile_rows) {
    LOG_INFO("OfficeCharacterManager initializing...");
    tile_cols_ = tile_cols;
    tile_row_ = tile_rows;
    InitializeDefaultCharacters();
    LOG_INFO("OfficeCharacterManager initialized with 3 characters.");
}

void OfficeCharacterManager::InitializeDefaultCharacters() {
    // Coder - palette 0 (brown hair, blue clothes)
    CharacterInstance coder;
    coder.def.id = "coder";
    coder.def.name = "代码专家";
    coder.def.avatar = "👨‍💻";
    coder.def.palette_index = 0;
    coder.tile_col = 5;
    coder.tile_row = 10;
    coder.x = 5 * TILE_SIZE;
    coder.y = 10 * TILE_SIZE;
    characters_[0] = coder;

    // Reviewer - palette 1 (blonde hair, green clothes)
    CharacterInstance reviewer;
    reviewer.def.id = "reviewer";
    reviewer.def.name = "代码审查员";
    reviewer.def.avatar = "🧐";
    reviewer.def.palette_index = 1;
    reviewer.tile_col = 8;
    reviewer.tile_row = 10;
    reviewer.x = 8 * TILE_SIZE;
    reviewer.y = 10 * TILE_SIZE;
    characters_[1] = reviewer;

    // Architect - palette 5 (purple hair, red clothes)
    CharacterInstance architect;
    architect.def.id = "architect";
    architect.def.name = "架构师";
    architect.def.avatar = "🏗️";
    architect.def.palette_index = 5;
    architect.tile_col = 11;
    architect.tile_row = 10;
    architect.x = 11 * TILE_SIZE;
    architect.y = 10 * TILE_SIZE;
    characters_[2] = architect;

    // Assign default seats (desk positions)
    AssignSeat("coder", 5, 8, CharDirection::UP);
    AssignSeat("reviewer", 8, 8, CharDirection::UP);
    AssignSeat("architect", 11, 8, CharDirection::UP);
}

void OfficeCharacterManager::AssignSeat(const std::string& role_id, int seat_col, int seat_row,
                                         CharDirection facing) {
    if (auto* ch = GetCharacter(role_id)) {
        ch->seat_id = "seat_" + role_id;
        ch->seat_col = seat_col;
        ch->seat_row = seat_row;
        // When assigned to seat, face the desk
        ch->direction = facing;
        LOG_INFO("Assigned {} to seat at ({}, {})", role_id, seat_col, seat_row);
    }
}

CharacterInstance* OfficeCharacterManager::GetCharacter(const std::string& role_id) {
    for (auto& ch : characters_) {
        if (ch.def.id == role_id) return &ch;
    }
    return nullptr;
}

void OfficeCharacterManager::SetCharacterActivity(const std::string& role_id, bool is_active,
                                                   const std::string& current_tool) {
    if (auto* ch = GetCharacter(role_id)) {
        ch->is_active = is_active;
        ch->current_tool = current_tool;

        // Determine animation state based on tool
        if (is_active) {
            // Reading tools vs typing tools
            static const std::set<std::string> reading_tools = {
                "Read", "Grep", "Glob", "WebFetch", "WebSearch"
            };
            if (reading_tools.count(current_tool)) {
                ch->state = CharacterState::READ;
            } else {
                ch->state = CharacterState::TYPE;
            }
            ch->frame = 0;
            ch->frame_timer = 0;
        } else {
            // Not active - return to idle or walk to seat
            if (!ch->seat_id.empty()) {
                // Pathfind back to seat
                auto path = FindPath(ch->tile_col, ch->tile_row, ch->seat_col, ch->seat_row);
                if (!path.empty()) {
                    ch->path = path;
                    ch->move_progress = 0;
                    ch->state = CharacterState::WALK;
                } else {
                    ch->state = CharacterState::IDLE;
                }
            } else {
                ch->state = CharacterState::IDLE;
            }
        }
    }
}

void OfficeCharacterManager::SetCharacterState(const std::string& role_id, CharacterState state) {
    if (auto* ch = GetCharacter(role_id)) {
        ch->state = state;
        ch->frame = 0;
        ch->frame_timer = 0;
    }
}

void OfficeCharacterManager::UpdateCharacter(CharacterInstance& ch, float dt) {
    // State machine (reference: pixel-agents characters.ts)
    switch (ch.state) {
        case CharacterState::TYPE:
        case CharacterState::READ: {
            // Animate typing/reading (2 frames)
            ch.frame_timer += dt;
            const float frame_duration = (ch.state == CharacterState::READ)
                ? TYPE_FRAME_DURATION * 1.5f
                : TYPE_FRAME_DURATION;

            if (ch.frame_timer >= frame_duration) {
                ch.frame_timer -= frame_duration;
                ch.frame = (ch.frame + 1) % 2;
            }

            // If became inactive, stand up
            if (!ch.is_active) {
                ch.state = CharacterState::IDLE;
                ch.frame = 0;
                ch.frame_timer = 0;
            }
            break;
        }

        case CharacterState::IDLE: {
            ch.frame = 0;

            // If became active, pathfind to seat
            if (ch.is_active && !ch.seat_id.empty()) {
                auto path = FindPath(ch.tile_col, ch.tile_row, ch.seat_col, ch.seat_row);
                if (!path.empty()) {
                    ch.path = path;
                    ch.move_progress = 0;
                    ch.state = CharacterState::WALK;
                    ch.frame = 0;
                    ch.frame_timer = 0;
                } else {
                    // Already at seat - start typing
                    ch.state = CharacterState::TYPE;
                    ch.direction = CharDirection::UP;
                }
            } else if (!ch.is_active) {
                // IDLE 时自由活动：随机移动到附近位置
                ch.idle_timer += dt;
                const float IDLE_MOVE_INTERVAL = 2.0f;  // 每 2 秒尝试移动一次

                if (ch.idle_timer >= IDLE_MOVE_INTERVAL && ch.path.empty()) {
                    ch.idle_timer = 0;

                    // 随机选择移动方向（上下左右）
                    static std::random_device rd;
                    static std::mt19937 gen(rd());
                    static std::uniform_int_distribution<> dir_dist(0, 3);
                    static std::uniform_int_distribution<> step_dist(1, 3);

                    int dcol[] = {0, 0, 1, -1};
                    int drow[] = {1, -1, 0, 0};

                    int dir_idx = dir_dist(gen);
                    int steps = step_dist(gen);

                    int new_col = ch.tile_col + dcol[dir_idx] * steps;
                    int new_row = ch.tile_row + drow[dir_idx] * steps;

                    // 检查目标位置是否可行走
                    if (IsWalkable(new_col, new_row)) {
                        auto path = FindPath(ch.tile_col, ch.tile_row, new_col, new_row);
                        if (!path.empty()) {
                            ch.path = path;
                            ch.move_progress = 0;
                            ch.state = CharacterState::WALK;
                            ch.frame = 0;
                            ch.frame_timer = 0;
                        }
                    }
                }
            }
            break;
        }

        case CharacterState::WALK: {
            // Walk animation (4 frames)
            ch.frame_timer += dt;
            if (ch.frame_timer >= WALK_FRAME_DURATION) {
                ch.frame_timer -= WALK_FRAME_DURATION;
                ch.frame = (ch.frame + 1) % 4;
            }

            if (ch.path.empty()) {
                // Path complete - snap to tile center
                auto [cx, cy] = TileCenter(ch.tile_col, ch.tile_row);
                ch.x = cx;
                ch.y = cy;

                // Check if arrived at seat
                if (ch.is_active && ch.tile_col == ch.seat_col && ch.tile_row == ch.seat_row) {
                    ch.state = CharacterState::TYPE;
                } else if (!ch.is_active && !ch.seat_id.empty() &&
                           ch.tile_col == ch.seat_col && ch.tile_row == ch.seat_row) {
                    // Arrived at seat while inactive - sit down
                    ch.state = CharacterState::IDLE;
                } else {
                    ch.state = CharacterState::IDLE;
                }
                ch.frame = 0;
                ch.frame_timer = 0;
                break;
            }

            // Move toward next tile
            const auto [next_col, next_row] = ch.path[0];

            // Determine direction
            int dc = next_col - ch.tile_col;
            int dr = next_row - ch.tile_row;
            if (dc > 0) ch.direction = CharDirection::RIGHT;
            else if (dc < 0) ch.direction = CharDirection::LEFT;
            else if (dr > 0) ch.direction = CharDirection::DOWN;
            else ch.direction = CharDirection::UP;

            ch.move_progress += (WALK_SPEED_PX_PER_SEC / TILE_SIZE) * dt;

            auto [from_x, from_y] = TileCenter(ch.tile_col, ch.tile_row);
            auto [to_x, to_y] = TileCenter(next_col, next_row);

            float t = std::min(ch.move_progress, 1.0f);
            ch.x = from_x + (to_x - from_x) * t;
            ch.y = from_y + (to_y - from_y) * t;

            if (ch.move_progress >= 1.0f) {
                // Arrived at next tile
                ch.tile_col = next_col;
                ch.tile_row = next_row;
                ch.path.erase(ch.path.begin());
                ch.move_progress = 0;
            }
            break;
        }
    }
}

void OfficeCharacterManager::Update(float dt) {
    for (auto& ch : characters_) {
        UpdateCharacter(ch, dt);
    }
}

void OfficeCharacterManager::Render() {
    auto& renderer = CharacterSpriteRenderer::Instance();

    // Depth-sort characters by Y position
    std::vector<CharacterInstance*> sorted;
    for (auto& ch : characters_) {
        sorted.push_back(&ch);
    }
    std::sort(sorted.begin(), sorted.end(),
        [](const CharacterInstance* a, const CharacterInstance* b) {
            return a->y < b->y;  // Lower Y renders first (top to bottom)
        });

    // Render each character
    for (auto* ch : sorted) {
        // Scale sprite for better visibility (32x64 pixels on screen)
        float scale = 2.0f;
        // Offset to center sprite on tile
        float offset_x = (TILE_SIZE - 16 * scale) / 2.0f;
        float offset_y = (TILE_SIZE - 32 * scale) / 2.0f;

        renderer.Render(*ch, ch->x + offset_x, ch->y + offset_y, scale);

        // Debug: render role label
        // (Could use SDL_ttf here for text rendering)
    }
}

// Simple BFS pathfinding
std::vector<std::pair<int, int>> OfficeCharacterManager::FindPath(
    int from_col, int from_row, int to_col, int to_row) {

    if (from_col == to_col && from_row == to_row) {
        return {};  // Already at destination
    }

    // Check bounds
    if (to_col < 0 || to_col >= tile_cols_ || to_row < 0 || to_row >= tile_row_) {
        return {};
    }

    // BFS
    std::queue<std::pair<int, int>> queue;
    std::set<std::pair<int, int>> visited;
    std::map<std::pair<int, int>, std::pair<int, int>> came_from;

    queue.push({from_col, from_row});
    visited.insert({from_col, from_row});

    int dcol[] = {0, 0, 1, -1};
    int drow[] = {1, -1, 0, 0};

    while (!queue.empty()) {
        auto [col, row] = queue.front();
        queue.pop();

        if (col == to_col && row == to_row) {
            // Reconstruct path
            std::vector<std::pair<int, int>> path;
            std::pair<int, int> current = {to_col, to_row};
            while (current != std::make_pair(from_col, from_row)) {
                path.push_back(current);
                current = came_from[current];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (int i = 0; i < 4; i++) {
            int nc = col + dcol[i];
            int nr = row + drow[i];

            if (nc >= 0 && nc < tile_cols_ && nr >= 0 && nr < tile_row_ &&
                IsWalkable(nc, nr) && !visited.count({nc, nr})) {
                queue.push({nc, nr});
                visited.insert({nc, nr});
                came_from[{nc, nr}] = {col, row};
            }
        }
    }

    return {};  // No path found
}

bool OfficeCharacterManager::IsWalkable(int col, int row) const {
    // Check bounds
    if (col < 0 || col >= tile_cols_ || row < 0 || row >= tile_row_) {
        return false;
    }

    // For now, all tiles are walkable (will integrate with tile map later)
    return true;
}

std::pair<float, float> OfficeCharacterManager::TileCenter(int col, int row) const {
    // 使用 LayoutConfig 中的 tile_size，并加上办公室区域偏移
    float tile_size = LayoutConfig{}.tile_size;
    float office_x = LayoutConfig::GetOfficeX();
    float office_y = LayoutConfig::GetOfficeY();

    return {
        office_x + col * tile_size + tile_size / 2.0f,
        office_y + row * tile_size + tile_size / 2.0f
    };
}

}  // namespace aicode
