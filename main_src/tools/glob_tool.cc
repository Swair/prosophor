// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/glob_tool.h"

#include <filesystem>
#include <regex>
#include <algorithm>

#include "common/log_wrapper.h"

namespace aicode {

namespace fs = std::filesystem;

GlobTool& GlobTool::GetInstance() {
    static GlobTool instance;
    return instance;
}

// Convert glob pattern to regex
std::string GlobToRegex(const std::string& glob) {
    std::string regex = "^";
    size_t i = 0;
    while (i < glob.size()) {
        switch (glob[i]) {
            case '*':
                if (i + 1 < glob.size() && glob[i + 1] == '*') {
                    // ** matches any path
                    if (i + 2 < glob.size() && glob[i + 2] == '/') {
                        regex += "(?:.*/)?";
                        i += 3;
                        continue;
                    }
                    regex += ".*";
                    i += 2;
                } else {
                    // * matches anything except /
                    regex += "[^/]*";
                    i++;
                }
                break;
            case '?':
                regex += "[^/]";
                i++;
                break;
            case '.':
            case '^':
            case '$':
            case '+':
            case '{':
            case '}':
            case '[':
            case ']':
            case '(':
            case ')':
            case '|':
                regex += '\\';
                regex += glob[i];
                i++;
                break;
            default:
                regex += glob[i];
                i++;
        }
    }
    regex += "$";
    return regex;
}

std::vector<std::string> GlobTool::Search(const std::string& pattern, const std::string& root_dir) {
    std::vector<std::string> results;

    std::string search_root = root_dir.empty() ? fs::current_path().string() : root_dir;

    try {
        // Convert glob to regex
        std::regex pattern_regex(GlobToRegex(pattern), std::regex::icase);

        for (const auto& entry : fs::recursive_directory_iterator(search_root)) {
            if (entry.is_regular_file()) {
                std::string rel_path = fs::relative(entry.path(), search_root).string();

                // Normalize path separators
                std::replace(rel_path.begin(), rel_path.end(), '\\', '/');

                if (std::regex_match(rel_path, pattern_regex)) {
                    results.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Glob search failed: {}", e.what());
    }

    // Sort results
    std::sort(results.begin(), results.end());

    LOG_INFO("Glob search '{}' found {} files", pattern, results.size());
    return results;
}

std::string GlobTool::Execute(const nlohmann::json& params) {
    std::string pattern = params.value("pattern", "");
    std::string root_dir = params.value("root_dir", "");

    if (pattern.empty()) {
        return R"({"error": "pattern is required"})";
    }

    auto results = Search(pattern, root_dir);

    nlohmann::json result;
    result["files"] = results;
    result["count"] = results.size();

    return result.dump(2);
}

}  // namespace aicode
