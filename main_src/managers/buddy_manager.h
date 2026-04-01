// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <cstdint>

#include "common/noncopyable.h"
#include "managers/buddy_types.h"

namespace aicode {

/// Buddy Manager - handles companion pet system
class BuddyManager : public Noncopyable {
public:
    static BuddyManager& GetInstance();

    /// Get current companion (loaded from config or newly generated)
    const Companion& GetCompanion() const { return companion_; }

    /// Check if companion is loaded
    bool HasCompanion() const { return companion_loaded_; }

    /// Generate new companion for user ID
    /// If user_id is empty, uses default ID
    Companion GenerateCompanion(const std::string& user_id = "");

    /// Load companion from config file
    /// If config_path is empty, uses AI_CODE_CONFIG env var or ~/.aicode/config.json
    bool LoadCompanion(const std::string& config_path = "");

    /// Save companion to config file
    /// If config_path is empty, uses AI_CODE_CONFIG env var or ~/.aicode/config.json
    bool SaveCompanion(const std::string& config_path = "") const;

    /// Set companion name (calls LLM to generate personality)
    void SetCompanionName(const std::string& name);

    /// Render a random buddy as ASCII art (for display only, doesn't save)
    std::vector<std::string> RenderRandomSprite(int frame = 0) const;

    /// Render current companion as ASCII art (5 lines, 12 chars wide)
    /// Returns vector of 4-5 lines (hat line omitted if no hat)
    std::vector<std::string> RenderSprite(int frame = 0) const;

    /// Render companion info as display string
    std::string RenderInfo() const;

    /// Get stat value by type
    int GetStat(StatType stat) const;

private:
    BuddyManager() = default;
    ~BuddyManager() = default;

    /// Generate deterministic bones from user ID hash
    void GenerateBones(const std::string& user_id);

    /// Roll rarity based on weights
    Rarity RollRarity(uint32_t seed) const;

    /// Generate stats based on species and rarity
    void GenerateStats();

    Companion companion_;
    bool companion_loaded_ = false;
};

}  // namespace aicode
