// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/buddy_manager.h"

#include <array>
#include <fstream>
#include <sstream>
#include <chrono>
#include <functional>
#include <random>

#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"
#include "common/config.h"

namespace aicode {

// Sprite frames - 3 frames per species, 5 lines tall, 12 chars wide
// {E} is replaced with eye character
static const std::array<std::array<std::array<const char*, 5>, 3>, 18> kSpriteFrames = {{
    // Duck
    {{
        {"            ", "    __      ", "  <({E} )___  ", "   (  ._>   ", "    `--´    "},
        {"            ", "    __      ", "  <({E} )___  ", "   (  ._>   ", "    `--´~   "},
        {"            ", "    __      ", "  <({E} )___  ", "   (  .__>  ", "    `--´    "},
    }},
    // Goose
    {{
        {"            ", "     ({E}>    ", "     ||     ", "   _(__)_   ", "    ^^^^    "},
        {"            ", "    ({E}>     ", "     ||     ", "   _(__)_   ", "    ^^^^    "},
        {"            ", "     ({E}>>   ", "     ||     ", "   _(__)_   ", "    ^^^^    "},
    }},
    // Blob
    {{
        {"            ", "   .----.   ", "  ( {E}  {E} )  ", "  (      )  ", "   `----´   "},
        {"            ", "  .------.  ", " (  {E}  {E}  ) ", " (        ) ", "  `------´  "},
        {"            ", "    .--.    ", "   ({E}  {E})   ", "   (    )   ", "    `--´    "},
    }},
    // Cat
    {{
        {"            ", "   /\\_/\\    ", "  ( {E}   {E})  ", "  (  ω  )   ", "  (\")_(\")   "},
        {"            ", "   /\\_/\\    ", "  ( {E}   {E})  ", "  (  ω  )   ", "  (\")_(\")~  "},
        {"            ", "   /\\-/\\    ", "  ( {E}   {E})  ", "  (  ω  )   ", "  (\")_(\")   "},
    }},
    // Dragon
    {{
        {"            ", "  /^\\  /^\\  ", " <  {E}  {E}  > ", " (   ~~   ) ", "  `-vvvv-´  "},
        {"            ", "  /^\\  /^\\  ", " <  {E}  {E}  > ", " (        ) ", "  `-vvvv-´  "},
        {"   ~    ~   ", "  /^\\  /^\\  ", " <  {E}  {E}  > ", " (   ~~   ) ", "  `-vvvv-´  "},
    }},
    // Octopus
    {{
        {"            ", "   .----.   ", "  ( {E}  {E} )  ", "  (______)  ", "  /\\/\\/\\/\\  "},
        {"            ", "   .----.   ", "  ( {E}  {E} )  ", "  (______)  ", "  \\/\\/\\/\\/  "},
        {"     o      ", "   .----.   ", "  ( {E}  {E} )  ", "  (______)  ", "  /\\/\\/\\/\\  "},
    }},
    // Owl
    {{
        {"            ", "   /\\  /\\   ", "  (({E})({E}))  ", "  (  ><  )  ", "   `----´   "},
        {"            ", "   /\\  /\\   ", "  (({E})({E}))  ", "  (  ><  )  ", "   .----.   "},
        {"            ", "   /\\  /\\   ", "  (({E})(-))  ", "  (  ><  )  ", "   `----´   "},
    }},
    // Penguin
    {{
        {"            ", "  .---.     ", "  ({E}>{E})     ", " /(   )\\    ", "  `---´     "},
        {"            ", "  .---.     ", "  ({E}>{E})     ", " |(   )|    ", "  `---´     "},
        {"  .---.     ", "  ({E}>{E})     ", " /(   )\\    ", "  `---´     ", "   ~ ~      "},
    }},
    // Turtle
    {{
        {"            ", "   _,--._   ", "  ( {E}  {E} )  ", " /[______]\\ ", "  ``    ``  "},
        {"            ", "   _,--._   ", "  ( {E}  {E} )  ", " /[______]\\ ", "   ``  ``   "},
        {"            ", "   _,--._   ", "  ( {E}  {E} )  ", " /[======]\\ ", "  ``    ``  "},
    }},
    // Snail
    {{
        {"            ", " {E}    .--.  ", "  \\  ( @ )  ", "   \\_`--´   ", "  ~~~~~~~   "},
        {"            ", "  {E}   .--.  ", "  |  ( @ )  ", "   \\_`--´   ", "  ~~~~~~~   "},
        {"            ", " {E}    .--.  ", "  \\  ( @  ) ", "   \\_`--´   ", "   ~~~~~~   "},
    }},
    // Ghost
    {{
        {"            ", "   .----.   ", "  / {E}  {E} \\  ", "  |      |  ", "  ~`~``~`~  "},
        {"            ", "   .----.   ", "  / {E}  {E} \\  ", "  |      |  ", "  `~`~~`~`  "},
        {"    ~  ~    ", "   .----.   ", "  / {E}  {E} \\  ", "  |      |  ", "  ~~`~~`~~  "},
    }},
    // Axolotl
    {{
        {"            ", "}~(______)~{", "}~({E} .. {E})~{", "  ( .--. )  ", "  (_/  \\_)  "},
        {"            ", "~}(______){~", "~}({E} .. {E}){~", "  ( .--. )  ", "  (_/  \\_)  "},
        {"            ", "}~(______)~{", "}~({E} .. {E})~{", "  (  --  )  ", "  ~_/  \\_~  "},
    }},
    // Capybara
    {{
        {"            ", "  n______n  ", " ( {E}    {E} ) ", " (   oo   ) ", "  `------´  "},
        {"            ", "  n______n  ", " ( {E}    {E} ) ", " (   Oo   ) ", "  `------´  "},
        {"    ~  ~    ", "  u______n  ", " ( {E}    {E} ) ", " (   oo   ) ", "  `------´  "},
    }},
    // Cactus
    {{
        {"            ", " n  ____  n ", " | |{E}  {E}| | ", " |_|    |_| ", "   |    |   "},
        {"            ", "    ____    ", " n |{E}  {E}| n ", " |_|    |_| ", "   |    |   "},
        {" n        n ", " |  ____  | ", " | |{E}  {E}| | ", " |_|    |_| ", "   |    |   "},
    }},
    // Robot
    {{
        {"            ", "   .[||].   ", "  [ {E}  {E} ]  ", "  [ ==== ]  ", "  `------´  "},
        {"            ", "   .[||].   ", "  [ {E}  {E} ]  ", "  [ -==- ]  ", "  `------´  "},
        {"     *      ", "   .[||].   ", "  [ {E}  {E} ]  ", "  [ ==== ]  ", "  `------´  "},
    }},
    // Rabbit
    {{
        {"            ", "   (\\__/)   ", "  ( {E}  {E} )  ", " =(  ..  )= ", "  (\")__(\")  "},
        {"            ", "   (|__/)   ", "  ( {E}  {E} )  ", " =(  ..  )= ", "  (\")__(\")  "},
        {"            ", "   (\\__/)   ", "  ( {E}  {E} )  ", " =( .  . )= ", "  (\")__(\")  "},
    }},
    // Mushroom
    {{
        {"            ", " .-o-OO-o-. ", "(__________)", "   |{E}  {E}|   ", "   |____|   "},
        {"            ", " .-O-oo-O-. ", "(__________)", "   |{E}  {E}|   ", "   |____|   "},
        {"   . o  .   ", " .-o-OO-o-. ", "(__________)", "   |{E}  {E}|   ", "   |____|   "},
    }},
    // Chonk
    {{
        {"            ", "  /\\    /\\  ", " ( {E}    {E} ) ", " (   ..   ) ", "  `------´  "},
        {"            ", "  /\\    /|  ", " ( {E}    {E} ) ", " (   ..   ) ", "  `------´  "},
        {"            ", "  /\\    /\\  ", " ( {E}    {E} ) ", " (   ..   ) ", "  `------´~ "},
    }},
}};

static const std::array<const char*, 8> kHatLines = {
    "            ",  // None
    "   \\^^^/    ",  // Crown
    "   [___]    ",  // Tophat
    "    -+-     ",  // Propeller
    "   (   )    ",  // Halo
    "    /^\\     ",  // Wizard
    "   (___)    ",  // Beanie
    "    ,>      ",  // Tinyduck
};

BuddyManager& BuddyManager::GetInstance() {
    static BuddyManager instance;
    return instance;
}

void BuddyManager::GenerateBones(const std::string& user_id) {
    // Hash user ID to get deterministic seed
    std::hash<std::string> hasher;
    uint32_t seed = static_cast<uint32_t>(hasher(user_id));

    // Roll species (0-17)
    int species_idx = seed % 18;
    companion_.species = static_cast<Species>(species_idx);

    // Roll rarity based on weights
    companion_.rarity = RollRarity(seed >> 4);

    // Roll eye (0-5)
    int eye_idx = (seed >> 8) % 6;
    companion_.eye = static_cast<Eye>(eye_idx);

    // Roll hat (0-7, 75% chance of none)
    if ((seed >> 12) % 4 == 0) {
        int hat_idx = 1 + ((seed >> 16) % 7);  // 1-7, not 0 (none)
        companion_.hat = static_cast<Hat>(hat_idx);
    } else {
        companion_.hat = Hat::None;
    }

    // Roll shiny (1% chance)
    companion_.shiny = ((seed >> 20) % 100) == 0;

    // Generate stats
    GenerateStats();
}

Rarity BuddyManager::RollRarity(uint32_t seed) const {
    int roll = seed % 100;
    if (roll < 60) return Rarity::Common;
    if (roll < 85) return Rarity::Uncommon;
    if (roll < 95) return Rarity::Rare;
    if (roll < 99) return Rarity::Epic;
    return Rarity::Legendary;
}

void BuddyManager::GenerateStats() {
    // Base stats by species (some species have natural tendencies)
    int base_debug = 50, base_pat = 50, base_chaos = 50, base_wis = 50, base_snark = 50;

    switch (companion_.species) {
        case Species::Robot:
        case Species::Owl:
            base_wis = 70;
            base_snark = 30;
            break;
        case Species::Cat:
        case Species::Dragon:
            base_chaos = 70;
            base_pat = 30;
            break;
        case Species::Capybara:
        case Species::Blob:
            base_pat = 80;
            base_chaos = 20;
            break;
        case Species::Ghost:
        case Species::Axolotl:
            base_snark = 70;
            base_wis = 30;
            break;
        default:
            break;
    }

    // Rarity bonus
    int rarity_bonus = static_cast<int>(companion_.rarity) * 5;

    companion_.debugging = std::min(100, base_debug + rarity_bonus);
    companion_.patience = std::min(100, base_pat + rarity_bonus);
    companion_.chaos = std::min(100, base_chaos + rarity_bonus);
    companion_.wisdom = std::min(100, base_wis + rarity_bonus);
    companion_.snark = std::min(100, base_snark + rarity_bonus);
}

Companion BuddyManager::GenerateCompanion(const std::string& user_id) {
    std::string uid = user_id.empty() ? "default_user" : user_id;
    GenerateBones(uid);

    companion_.hatched_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    companion_loaded_ = true;
    LOG_INFO("Generated new companion: {} ({}, {})",
             BuddyUtils::GetSpeciesName(companion_.species),
             BuddyUtils::GetRarityName(companion_.rarity),
             companion_.shiny ? "shiny" : "normal");

    return companion_;
}

bool BuddyManager::LoadCompanion(const std::string& config_path) {
    std::string path = config_path.empty() ? (AiCodeConfig::BaseDir() / "config.json").string() : config_path;

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_WARN("Companion config not found: {}", path);
            return false;
        }

        nlohmann::json json;
        file >> json;

        if (!json.contains("buddy")) {
            LOG_WARN("No buddy data in config");
            return false;
        }

        const auto& b = json["buddy"];
        companion_.name = b.value("name", "");
        companion_.personality = b.value("personality", "");
        companion_.hatched_at = b.value("hatched_at", int64_t(0));

        // Bones are regenerated from user ID hash
        std::string user_id = json.value("user_id", "default_user");
        GenerateBones(user_id);

        companion_loaded_ = true;
        LOG_INFO("Loaded companion: {}", companion_.name);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load companion: {}", e.what());
        return false;
    }
}

bool BuddyManager::SaveCompanion(const std::string& config_path) const {
    std::string path = config_path.empty() ? (AiCodeConfig::BaseDir() / "config.json").string() : config_path;

    try {
        nlohmann::json json;
        json["buddy"]["name"] = companion_.name;
        json["buddy"]["personality"] = companion_.personality;
        json["buddy"]["hatched_at"] = companion_.hatched_at;

        std::ofstream file(config_path);
        if (!file.is_open()) {
            LOG_ERROR("Cannot open config for writing: {}", config_path);
            return false;
        }

        file << json.dump(2);
        LOG_INFO("Saved companion: {}", companion_.name);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save companion: {}", e.what());
        return false;
    }
}

std::vector<std::string> BuddyManager::RenderSprite(int frame) const {
    std::vector<std::string> lines;

    int species_idx = static_cast<int>(companion_.species);
    if (species_idx < 0 || species_idx >= 18) {
        return {"Sprite error: invalid species"};
    }

    char eye_char = BuddyUtils::GetEyeChar(companion_.eye);
    const auto& sprite = kSpriteFrames[species_idx][frame % 3];

    // Add hat line if hat is not none
    if (companion_.hat != Hat::None) {
        lines.push_back(kHatLines[static_cast<int>(companion_.hat)]);
    }

    // Add body lines with eye substitution
    for (int i = 0; i < 5; i++) {
        std::string line = sprite[i];
        // Replace {E} with eye character
        size_t pos = 0;
        while ((pos = line.find("{E}", pos)) != std::string::npos) {
            line.replace(pos, 3, std::string(1, eye_char));
        }
        lines.push_back(line);
    }

    return lines;
}

std::vector<std::string> BuddyManager::RenderRandomSprite(int frame) const {
    std::vector<std::string> lines;

    // Random species (0-17)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> species_dist(0, 17);
    int species_idx = species_dist(gen);

    // Random eye (0-5)
    std::uniform_int_distribution<> eye_dist(0, 5);
    int eye_idx = eye_dist(gen);
    char eye_char = BuddyUtils::GetEyeChar(static_cast<Eye>(eye_idx));

    // Random hat (0-7, 75% chance of none)
    std::uniform_int_distribution<> hat_dist(0, 7);
    int hat_idx = hat_dist(gen);

    const auto& sprite = kSpriteFrames[species_idx][frame % 3];

    // Add hat line if hat is not none
    if (hat_idx != 0) {
        lines.push_back(kHatLines[hat_idx]);
    }

    // Add body lines with eye substitution
    for (int i = 0; i < 5; i++) {
        std::string line = sprite[i];
        // Replace {E} with eye character
        size_t pos = 0;
        while ((pos = line.find("{E}", pos)) != std::string::npos) {
            line.replace(pos, 3, std::string(1, eye_char));
        }
        lines.push_back(line);
    }

    return lines;
}

std::string BuddyManager::RenderInfo() const {
    std::ostringstream ss;

    ss << "╔════════════════════════════════════╗\n";
    ss << "║         YOUR COMPANION             ║\n";
    ss << "╠════════════════════════════════════╣\n";

    std::string display_name = companion_.name.empty() ? "Unnamed" : companion_.name;
    ss << "║ Name: " << display_name;
    for (size_t i = display_name.size(); i < 27; i++) ss << " ";
    ss << "║\n";

    std::string species = BuddyUtils::GetSpeciesName(companion_.species);
    std::string rarity = BuddyUtils::GetRarityName(companion_.rarity);
    std::string stars = BuddyUtils::GetRarityStars(companion_.rarity);

    ss << "║ " << species << " " << stars;
    for (size_t i = species.size() + stars.size() + 1; i < 18; i++) ss << " ";
    ss << rarity;
    for (size_t i = rarity.size(); i < 8; i++) ss << " ";
    ss << "║\n";

    if (companion_.shiny) {
        ss << "║ ✨ SHINY ✨                        ║\n";
    }

    ss << "╠════════════════════════════════════╣\n";
    ss << "║ Stats:                             ║\n";
    ss << "║   DEBUGGING: " << companion_.debugging;
    for (size_t i = 0; i < 3 - std::to_string(companion_.debugging).size(); i++) ss << " ";
    ss << " PATIENCE: " << companion_.patience;
    for (size_t i = 0; i < 3 - std::to_string(companion_.patience).size(); i++) ss << " ";
    ss << "║\n";
    ss << "║   CHAOS: " << companion_.chaos;
    for (size_t i = 0; i < 3 - std::to_string(companion_.chaos).size(); i++) ss << " ";
    ss << "       WISDOM: " << companion_.wisdom;
    for (size_t i = 0; i < 3 - std::to_string(companion_.wisdom).size(); i++) ss << " ";
    ss << "║\n";
    ss << "║   SNARK: " << companion_.snark;
    for (size_t i = 0; i < 3 - std::to_string(companion_.snark).size(); i++) ss << " ";
    ss << "                          ║\n";

    ss << "╚════════════════════════════════════╝";

    return ss.str();
}

int BuddyManager::GetStat(StatType stat) const {
    switch (stat) {
        case StatType::Debugging: return companion_.debugging;
        case StatType::Patience: return companion_.patience;
        case StatType::Chaos: return companion_.chaos;
        case StatType::Wisdom: return companion_.wisdom;
        case StatType::Snark: return companion_.snark;
        default: return 0;
    }
}

void BuddyManager::SetCompanionName(const std::string& name) {
    companion_.name = name;
    LOG_INFO("Companion named: {}", name);
}

}  // namespace aicode
