// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace aicode {

/// GrepTool - Search for text/pattern in files
class GrepTool {
public:
    static GrepTool& GetInstance();

    /// Search for pattern in files
    /// @param pattern Regex pattern to search for
    /// @param path Path to search (file or directory)
    /// @param include Glob pattern for file inclusion (e.g., "*.cc")
    /// @param exclude Glob pattern for file exclusion (e.g., "*.min.js")
    /// @param max_results Maximum number of results to return (default: 100)
    /// @return JSON array of match objects {file, line, content}
    std::vector<nlohmann::json> Search(
        const std::string& pattern,
        const std::string& path = ".",
        const std::string& include = "",
        const std::string& exclude = "",
        int max_results = 100);

    /// Execute tool and return JSON result
    std::string Execute(const nlohmann::json& params);

private:
    GrepTool() = default;

    /// Check if file matches include/exclude patterns
    bool ShouldIncludeFile(const std::string& filepath,
                          const std::string& include,
                          const std::string& exclude) const;

    /// Escape special regex characters
    std::string EscapeRegex(const std::string& s) const;
};

}  // namespace aicode
