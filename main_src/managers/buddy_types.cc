// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/buddy_types.h"

#include <array>
#include <functional>

namespace aicode {

// Species names
static const std::array<const char*, 18> kSpeciesNames = {
    "duck", "goose", "blob", "cat", "dragon", "octopus",
    "owl", "penguin", "turtle", "snail", "ghost", "axolotl",
    "capybara", "cactus", "robot", "rabbit", "mushroom", "chonk"
};

// Rarity names
static const std::array<const char*, 5> kRarityNames = {
    "common", "uncommon", "rare", "epic", "legendary"
};

// Eye characters (ASCII safe)
static const std::array<char, 6> kEyeChars = {
    '*', '+', 'x', 'o', '@', '.'
};

// Rarity weights (percentage)
static const std::array<int, 5> kRarityWeights = {
    60, 25, 10, 4, 1
};

std::string BuddyUtils::GetSpeciesName(Species species) {
    int idx = static_cast<int>(species);
    if (idx >= 0 && idx < 18) {
        return kSpeciesNames[idx];
    }
    return "unknown";
}

std::string BuddyUtils::GetRarityName(Rarity rarity) {
    int idx = static_cast<int>(rarity);
    if (idx >= 0 && idx < 5) {
        return kRarityNames[idx];
    }
    return "unknown";
}

char BuddyUtils::GetEyeChar(Eye eye) {
    int idx = static_cast<int>(eye);
    if (idx >= 0 && idx < 6) {
        return kEyeChars[idx];
    }
    return '·';
}

std::string BuddyUtils::GetHatLine(Hat hat) {
    // 12 characters wide, line 0 of sprite
    switch (hat) {
        case Hat::Crown:     return "   \\^^^/    ";
        case Hat::Tophat:    return "   [___]    ";
        case Hat::Propeller: return "    -+-     ";
        case Hat::Halo:      return "   (   )    ";
        case Hat::Wizard:    return "    /^\\     ";
        case Hat::Beanie:    return "   (___)    ";
        case Hat::Tinyduck:  return "    ,>      ";
        case Hat::None:
        default:             return "            ";
    }
}

std::string BuddyUtils::GetRarityStars(Rarity rarity) {
    switch (rarity) {
        case Rarity::Common:     return "★";
        case Rarity::Uncommon:   return "★★";
        case Rarity::Rare:       return "★★★";
        case Rarity::Epic:       return "★★★★";
        case Rarity::Legendary:  return "★★★★★";
        default:                 return "";
    }
}

std::string BuddyUtils::GetRarityColor(Rarity rarity) {
    switch (rarity) {
        case Rarity::Common:     return "gray";
        case Rarity::Uncommon:   return "green";
        case Rarity::Rare:       return "blue";
        case Rarity::Epic:       return "purple";
        case Rarity::Legendary:  return "yellow";
        default:                 return "white";
    }
}

}  // namespace aicode
