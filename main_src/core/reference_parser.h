// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <optional>

namespace aicode {

/// Line range for file references
struct LineRange {
    size_t start = 0;
    size_t end = 0;  // 0 means end of file

    bool IsValid() const { return start > 0 && (end == 0 || end >= start); }
};

/// File reference extracted from user input
struct FileReference {
    std::string path;           // Original path from input (e.g., "main.cc" or "main.cc:10-20")
    std::string full_path;      // Resolved absolute path
    std::string content;        // File content
    std::optional<LineRange> range;  // Optional line range
    bool exists = false;        // Whether file exists
    std::string error;          // Error message if file doesn't exist or can't be read
};

/// Session reference extracted from user input
struct SessionReference {
    std::string session_id;     // Session ID or name
    std::string content;        // Session content/summary
    bool exists = false;        // Whether session exists
    std::string error;          // Error message
};

/// Reference parser for @file and #session references
class ReferenceParser {
public:
    /// Extract file references from input
    /// @param input User input text
    /// @param workspace Workspace root path for resolving relative paths
    static std::vector<FileReference> ParseFileRefs(const std::string& input,
                                                     const std::string& workspace);

    /// Extract session references from input
    /// @param input User input text
    static std::vector<SessionReference> ParseSessionRefs(const std::string& input);

    /// Replace @file references with actual file content in input
    /// @param input Original input text
    /// @param refs Parsed file references with content loaded
    /// @return Input with @file replaced by content
    static std::string ReplaceFileRefs(const std::string& input,
                                        const std::vector<FileReference>& refs);

    /// Parse a single file reference string (e.g., "@main.cc:10-20")
    /// @param ref_str Reference string (with or without @ prefix)
    /// @param workspace Workspace root path
    static FileReference ParseSingleRef(const std::string& ref_str,
                                         const std::string& workspace);

    /// Check if input contains any file references
    static bool HasFileRefs(const std::string& input);

    /// Check if input contains any session references
    static bool HasSessionRefs(const std::string& input);

private:
    /// Extract line range from path string (e.g., "file.cc:10-20" -> range 10-20)
    static std::pair<std::string, std::optional<LineRange>> ExtractLineRange(const std::string& path_str);

    /// Read file content with optional line range
    static std::string ReadFileContent(const std::string& path,
                                        const std::optional<LineRange>& range);

    /// Resolve relative path against workspace
    static std::string ResolvePath(const std::string& path,
                                    const std::string& workspace);
};

}  // namespace aicode
