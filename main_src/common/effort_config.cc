// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/effort_config.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace aicode {

namespace {
// Global effort configuration state
EffortLevel g_current_effort = EffortLevel::Medium;
}  // namespace

EffortLevel EffortConfig::GetCurrent() {
    return g_current_effort;
}

void EffortConfig::Set(EffortLevel level) {
    g_current_effort = level;
}

bool EffortConfig::SetFromString(const std::string& level_str) {
    auto level = ParseLevel(level_str);
    if (level == EffortLevel::Auto && level_str != "auto") {
        return false;  // Invalid level string
    }
    Set(level);
    return true;
}

std::string EffortConfig::ToString(EffortLevel level) {
    switch (level) {
        case EffortLevel::Low:    return "low";
        case EffortLevel::Medium: return "medium";
        case EffortLevel::High:   return "high";
        case EffortLevel::Max:    return "max";
        case EffortLevel::Auto:   return "auto";
        default:                  return "medium";
    }
}

std::string EffortConfig::Description(EffortLevel level) {
    switch (level) {
        case EffortLevel::Low:
            return "Fast, minimal reasoning. Best for simple tasks and quick fixes.";
        case EffortLevel::Medium:
            return "Balanced approach. Good for most coding tasks.";
        case EffortLevel::High:
            return "Thorough reasoning and verification. Use for complex refactors and architectural decisions.";
        case EffortLevel::Max:
            return "Maximum capability. Uses full reasoning depth. Only available with Claude Opus 4.6.";
        case EffortLevel::Auto:
            return "Use model default effort level.";
        default:
            return "Unknown effort level.";
    }
}

std::vector<std::string> EffortConfig::GetValidLevels() {
    return {"low", "medium", "high", "max", "auto"};
}

bool EffortConfig::IsValidLevel(const std::string& level_str) {
    auto valid_levels = GetValidLevels();
    return std::find(valid_levels.begin(), valid_levels.end(), level_str) != valid_levels.end();
}

EffortLevel EffortConfig::ParseLevel(const std::string& level_str) {
    if (level_str == "low")    return EffortLevel::Low;
    if (level_str == "medium") return EffortLevel::Medium;
    if (level_str == "high")   return EffortLevel::High;
    if (level_str == "max")    return EffortLevel::Max;
    if (level_str == "auto")   return EffortLevel::Auto;
    return EffortLevel::Auto;  // Default to auto for unknown
}

nlohmann::json EffortConfig::ToJson() {
    nlohmann::json j;
    j["level"] = ToString(GetCurrent());
    j["description"] = Description(GetCurrent());
    return j;
}

}  // namespace aicode
