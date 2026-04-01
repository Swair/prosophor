// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace aicode {

/// Effort level enum for model reasoning depth control
enum class EffortLevel {
    Low,      // Fast, minimal reasoning
    Medium,   // Balanced approach (default)
    High,     // Thorough reasoning and verification
    Max,      // Maximum capability (Opus 4.6 only)
    Auto      // Use model default
};

/// Effort configuration for controlling model behavior
struct EffortConfig {
    EffortLevel level = EffortLevel::Medium;
    bool supported = true;

    /// Get current effort level
    static EffortLevel GetCurrent();

    /// Set effort level
    static void Set(EffortLevel level);

    /// Set effort level from string
    static bool SetFromString(const std::string& level_str);

    /// Get string representation of effort level
    static std::string ToString(EffortLevel level);

    /// Get description of effort level
    static std::string Description(EffortLevel level);

    /// Get all valid effort level strings
    static std::vector<std::string> GetValidLevels();

    /// Check if a level string is valid
    static bool IsValidLevel(const std::string& level_str);

    /// Parse string to EffortLevel
    static EffortLevel ParseLevel(const std::string& level_str);

    /// Get effort config as JSON for API request
    static nlohmann::json ToJson();
};

}  // namespace aicode
