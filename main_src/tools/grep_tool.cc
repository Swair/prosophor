// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/grep_tool.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <algorithm>

#include "common/log_wrapper.h"
#include "tools/glob_tool.h"

namespace aicode {

namespace fs = std::filesystem;

GrepTool& GrepTool::GetInstance() {
    static GrepTool instance;
    return instance;
}

std::string GrepTool::EscapeRegex(const std::string& s) const {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '\\': case '^': case '$': case '.':
            case '|': case '?': case '*': case '+':
            case '(': case ')': case '[': case ']':
            case '{': case '}':
                result += '\\';
                result += c;
                break;
            default:
                result += c;
        }
    }
    return result;
}

bool GrepTool::ShouldIncludeFile(const std::string& filepath,
                                 const std::string& include,
                                 const std::string& exclude) const {
    // Check exclude pattern
    if (!exclude.empty()) {
        std::regex exclude_regex(GlobToRegex(exclude), std::regex::icase);
        std::string filename = fs::path(filepath).filename().string();
        if (std::regex_match(filename, exclude_regex)) {
            return false;
        }
    }

    // Check include pattern
    if (!include.empty()) {
        std::regex include_regex(GlobToRegex(include), std::regex::icase);
        std::string filename = fs::path(filepath).filename().string();
        if (!std::regex_match(filename, include_regex)) {
            return false;
        }
    }

    return true;
}

std::vector<nlohmann::json> GrepTool::Search(
    const std::string& pattern,
    const std::string& path,
    const std::string& include,
    const std::string& exclude,
    int max_results) {

    std::vector<nlohmann::json> results;

    try {
        // Compile regex pattern
        std::regex pattern_regex(pattern, std::regex::icase);

        fs::path search_path = path;

        if (fs::is_regular_file(search_path)) {
            // Single file search
            if (!ShouldIncludeFile(path, include, exclude)) {
                return results;
            }

            std::ifstream file(path);
            std::string line;
            int line_num = 0;

            while (std::getline(file, line) && results.size() < max_results) {
                line_num++;
                if (std::regex_search(line, pattern_regex)) {
                    nlohmann::json match;
                    match["file"] = path;
                    match["line"] = line_num;
                    match["content"] = line;
                    results.push_back(match);
                }
            }
        } else if (fs::is_directory(search_path)) {
            // Directory search
            for (const auto& entry : fs::recursive_directory_iterator(search_path)) {
                if (results.size() >= max_results) break;

                if (!entry.is_regular_file()) continue;

                std::string filepath = entry.path().string();
                if (!ShouldIncludeFile(filepath, include, exclude)) continue;

                std::ifstream file(entry.path());
                std::string line;
                int line_num = 0;

                while (std::getline(file, line) && results.size() < max_results) {
                    line_num++;
                    if (std::regex_search(line, pattern_regex)) {
                        nlohmann::json match;
                        match["file"] = filepath;
                        match["line"] = line_num;
                        match["content"] = line;
                        results.push_back(match);
                    }
                }
            }
        }
    } catch (const std::regex_error& e) {
        LOG_ERROR("Regex error: {}", e.what());
        nlohmann::json error;
        error["error"] = std::string("Invalid regex pattern: ") + e.what();
        results.push_back(error);
    } catch (const std::exception& e) {
        LOG_ERROR("Grep search failed: {}", e.what());
        nlohmann::json error;
        error["error"] = e.what();
        results.push_back(error);
    }

    LOG_INFO("Grep search '{}' found {} matches", pattern, results.size());
    return results;
}

std::string GrepTool::Execute(const nlohmann::json& params) {
    std::string pattern = params.value("pattern", "");
    std::string path = params.value("path", ".");
    std::string include = params.value("include", "");
    std::string exclude = params.value("exclude", "");
    int max_results = params.value("max_results", 100);

    if (pattern.empty()) {
        return R"({"error": "pattern is required"})";
    }

    auto results = Search(pattern, path, include, exclude, max_results);

    nlohmann::json result;
    result["matches"] = results;
    result["count"] = results.size();

    return result.dump(2);
}

}  // namespace aicode
