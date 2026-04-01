// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace aicode {

/// Convert glob pattern to regex string
std::string GlobToRegex(const std::string& glob);

/// GlobTool - Search for files by pattern
class GlobTool {
public:
    static GlobTool& GetInstance();

    /// Search for files matching a glob pattern
    /// @param pattern Glob pattern (e.g., "**/*.cc", "src/**/*.h")
    /// @param root_dir Root directory to search from (default: current workspace)
    /// @return JSON array of matching file paths
    std::vector<std::string> Search(const std::string& pattern, const std::string& root_dir = "");

    /// Execute tool and return JSON result
    std::string Execute(const nlohmann::json& params);

private:
    GlobTool() = default;
};

}  // namespace aicode
