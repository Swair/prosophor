// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/reference_parser.h"

#include <regex>
#include <fstream>
#include <sstream>
#include <filesystem>

#include "common/log_wrapper.h"

namespace aicode {

namespace fs = std::filesystem;

namespace {
// Regex patterns
const std::regex FILE_REF_PATTERN(R"(@([^\s\:\"]+)(?::(\d+)(?:-(\d+))?)?)");
const std::regex SESSION_REF_PATTERN(R"(#([^\s]+))");
}  // namespace

std::pair<std::string, std::optional<LineRange>> ReferenceParser::ExtractLineRange(
    const std::string& path_str) {

    std::string path = path_str;
    std::optional<LineRange> range;

    // Look for :start-end or :start pattern
    auto colon_pos = path_str.find(':');
    if (colon_pos != std::string::npos) {
        path = path_str.substr(0, colon_pos);
        std::string range_str = path_str.substr(colon_pos + 1);

        auto dash_pos = range_str.find('-');
        if (dash_pos != std::string::npos) {
            // Range format: start-end
            try {
                LineRange lr;
                lr.start = std::stoul(range_str.substr(0, dash_pos));
                lr.end = std::stoul(range_str.substr(dash_pos + 1));
                if (lr.IsValid()) {
                    range = lr;
                }
            } catch (...) {
                // Invalid range, ignore
            }
        } else {
            // Single line format: start (treat as start-start)
            try {
                LineRange lr;
                lr.start = std::stoul(range_str);
                lr.end = lr.start;
                if (lr.IsValid()) {
                    range = lr;
                }
            } catch (...) {
                // Invalid line number, ignore
            }
        }
    }

    return {path, range};
}

std::string ReferenceParser::ResolvePath(const std::string& path,
                                          const std::string& workspace) {
    fs::path p(path);

    if (p.is_absolute()) {
        return p.string();
    }

    // Relative path - resolve against workspace
    fs::path resolved = fs::weakly_canonical(fs::path(workspace) / p);
    return resolved.string();
}

std::string ReferenceParser::ReadFileContent(const std::string& path,
                                              const std::optional<LineRange>& range) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    if (!range.has_value()) {
        // Read entire file
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Read specific line range
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    if (lines.empty()) {
        return "";
    }

    size_t start = range->start;
    size_t end = range->end > 0 ? range->end : lines.size();

    // Convert to 0-indexed
    if (start > 0) start--;
    if (end > 0) end--;

    // Bounds check
    if (start >= lines.size()) {
        return "";
    }
    end = std::min(end, lines.size() - 1);

    // Extract lines
    std::stringstream result;
    for (size_t i = start; i <= end && i < lines.size(); ++i) {
        result << lines[i] << "\n";
    }

    return result.str();
}

FileReference ReferenceParser::ParseSingleRef(const std::string& ref_str,
                                               const std::string& workspace) {
    FileReference ref;
    ref.path = ref_str;

    // Extract line range if present
    auto [path, range] = ExtractLineRange(ref_str);
    ref.range = range;

    // Resolve path
    ref.full_path = ResolvePath(path, workspace);

    // Check if file exists
    if (!fs::exists(ref.full_path)) {
        ref.exists = false;
        ref.error = "File not found: " + ref.full_path;
        return ref;
    }

    // Read content
    ref.content = ReadFileContent(ref.full_path, ref.range);
    if (ref.content.empty()) {
        ref.exists = true;  // File exists but is empty or range is invalid
        return ref;
    }

    ref.exists = true;
    return ref;
}

std::vector<FileReference> ReferenceParser::ParseFileRefs(
    const std::string& input,
    const std::string& workspace) {

    std::vector<FileReference> refs;
    std::smatch match;
    std::string::const_iterator search_start(input.cbegin());

    while (std::regex_search(search_start, input.cend(), match, FILE_REF_PATTERN)) {
        std::string ref_str = match[1].str();

        // Add line range info if present
        if (match[2].matched) {
            ref_str += ":";
            ref_str += match[2].str();
            if (match[3].matched) {
                ref_str += "-";
                ref_str += match[3].str();
            }
        }

        auto ref = ParseSingleRef(ref_str, workspace);
        refs.push_back(ref);

        search_start = match.suffix().first;
    }

    return refs;
}

std::vector<SessionReference> ReferenceParser::ParseSessionRefs(
    const std::string& input) {

    std::vector<SessionReference> refs;
    std::smatch match;
    std::string::const_iterator search_start(input.cbegin());

    // Note: Session reference implementation requires session manager
    // For now, just extract the session IDs
    while (std::regex_search(search_start, input.cend(), match, SESSION_REF_PATTERN)) {
        SessionReference ref;
        ref.session_id = match[1].str();
        ref.exists = false;
        ref.error = "Session reference not yet implemented";
        refs.push_back(ref);

        search_start = match.suffix().first;
    }

    return refs;
}

std::string ReferenceParser::ReplaceFileRefs(
    const std::string& input,
    const std::vector<FileReference>& refs) {

    std::string result = input;

    for (const auto& ref : refs) {
        if (!ref.exists) {
            // Replace with error message
            std::string placeholder = "@" + ref.path;
            std::string replacement = "\n[File not found: " + ref.path + "]\n";

            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), replacement);
                pos += replacement.length();
            }
            continue;
        }

        // Replace @path with file content
        std::string placeholder = "@" + ref.path;
        std::string replacement = "\n```" + fs::path(ref.path).extension().string().substr(1) +
                                  "\n" + ref.content + "```\n";

        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), replacement);
            pos += replacement.length();
        }
    }

    return result;
}

bool ReferenceParser::HasFileRefs(const std::string& input) {
    return std::regex_search(input, FILE_REF_PATTERN);
}

bool ReferenceParser::HasSessionRefs(const std::string& input) {
    return std::regex_search(input, SESSION_REF_PATTERN);
}

}  // namespace aicode
