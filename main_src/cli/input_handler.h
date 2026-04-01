// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>

namespace aicode {

/// Terminal input handler with tab completion
class InputHandler {
public:
    InputHandler();
    ~InputHandler();

    /// Enable raw mode for interactive input
    void EnableRawMode();

    /// Disable raw mode
    void DisableRawMode();

    /// Read a line with tab completion support
    /// Returns empty string on EOF (Ctrl+D)
    std::string ReadLine(const std::string& prompt = "> ");

    /// Set completion callback
    /// Callback receives current input and cursor position, returns completions
    using CompletionCallback = std::function<std::vector<std::string>(const std::string&, size_t)>;
    void SetCompletionCallback(CompletionCallback cb) { completion_callback_ = std::move(cb); }

    /// Set history file path
    void SetHistoryFile(const std::string& path) { history_file_ = path; }

    /// Load history from file
    void LoadHistory();

    /// Save history to file
    void SaveHistory();

    /// Add line to history
    void AddToHistory(const std::string& line);

    /// Check if input is complete (for multi-line input)
    bool IsInputComplete(const std::string& input) const;

private:
    bool raw_mode_enabled_ = false;
    std::string history_file_;
    std::vector<std::string> history_;
    size_t history_index_ = 0;
    CompletionCallback completion_callback_;

    // Current line buffer
    std::string buffer_;
    size_t cursor_pos_ = 0;

    // Completion state
    std::vector<std::string> completions_;
    size_t completion_index_ = 0;
    std::string completion_prefix_;
    std::string original_buffer_;  // Buffer before completion cycling

    /// Read a single character
    int ReadChar();

    /// Read multiple characters (for escape sequences)
    int ReadCharWithTimeout(int timeout_ms);

    /// Handle tab key for completion
    void HandleTab();

    /// Show completions below the input line
    void ShowCompletions();

    /// Refresh the current line display
    void RefreshLine(const std::string& prompt);

    /// Clear the current line display
    void ClearLine();

    /// Move cursor to position
    void MoveCursor(int columns);

    /// Handle up arrow (history previous)
    void HandleHistoryUp();

    /// Handle down arrow (history next)
    void HandleHistoryDown();

    /// Handle left arrow
    void HandleLeft();

    /// Handle right arrow
    void HandleRight();

    /// Handle home key
    void HandleHome();

    /// Handle end key
    void HandleEnd();

    /// Handle delete key
    void HandleDelete();

    /// Handle backspace
    void HandleBackspace();

    /// Find common prefix among completions
    static std::string FindCommonPrefix(const std::vector<std::string>& completions);
};

/// ANSI escape codes for terminal control
namespace ansi {
    inline const char* kClearScreen = "\033[2J\033[H";
    inline const char* kClearLine = "\033[2K";
    inline const char* kCursorHome = "\033[G";
    inline const char* kSaveCursor = "\033[s";
    inline const char* kRestoreCursor = "\033[u";
    inline const char* kHideCursor = "\033[?25l";
    inline const char* kShowCursor = "\033[?25h";

    inline std::string MoveCursorRight(int n) {
        return "\033[" + std::to_string(n) + "C";
    }

    inline std::string MoveCursorLeft(int n) {
        return "\033[" + std::to_string(n) + "D";
    }

    inline std::string Color(int code) {
        return "\033[" + std::to_string(code) + "m";
    }

    inline const char* kReset = "\033[0m";
    inline const char* kBold = "\033[1m";
    inline const char* kDim = "\033[2m";
}  // namespace ansi

}  // namespace aicode
