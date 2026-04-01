// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "cli/input_handler.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/log_wrapper.h"

namespace aicode {

// Terminal state storage
static termios orig_termios;
static bool termios_saved = false;

InputHandler::InputHandler() {
    // Set default history file
    const char* home = getenv("HOME");
    if (home) {
        history_file_ = std::string(home) + "/.aicode/history";
    }
}

InputHandler::~InputHandler() {
    DisableRawMode();
}

void InputHandler::EnableRawMode() {
    if (raw_mode_enabled_) return;

    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        LOG_ERROR("Failed to get terminal attributes");
        return;
    }
    termios_saved = true;

    termios raw = orig_termios;

    // Input modes: disable special handling
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Output modes: disable post-processing
    raw.c_oflag &= ~(OPOST);

    // Control modes: set 8-bit characters
    raw.c_cflag |= (CS8);

    // Local modes: disable echo, canonical mode, signal keys
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Control characters: VMIN=1, VTIME=0 for blocking read
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        LOG_ERROR("Failed to set raw mode");
        return;
    }

    raw_mode_enabled_ = true;
    LOG_DEBUG("Raw mode enabled");
}

void InputHandler::DisableRawMode() {
    if (!raw_mode_enabled_) return;

    if (termios_saved && tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        LOG_ERROR("Failed to restore terminal mode");
        return;
    }

    raw_mode_enabled_ = false;
    LOG_DEBUG("Raw mode disabled");
}

int InputHandler::ReadChar() {
    char c;
    while (read(STDIN_FILENO, &c, 1) != 1) {
        if (errno != EAGAIN) {
            return -1;
        }
    }
    return static_cast<unsigned char>(c);
}

int InputHandler::ReadCharWithTimeout(int timeout_ms) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int retval = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
    if (retval == -1 || retval == 0) {
        return -1;
    }

    return ReadChar();
}

void InputHandler::LoadHistory() {
    history_.clear();
    std::ifstream file(history_file_);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                history_.push_back(line);
            }
        }
        file.close();
        LOG_DEBUG("Loaded {} history entries", history_.size());
    }
    history_index_ = history_.size();
}

void InputHandler::SaveHistory() {
    // Create directory if needed
    size_t pos = history_file_.rfind('/');
    if (pos != std::string::npos) {
        std::string dir = history_file_.substr(0, pos);
        // Simple mkdir -p equivalent
        std::string cmd = "mkdir -p \"" + dir + "\"";
        system(cmd.c_str());
    }

    std::ofstream file(history_file_);
    if (file.is_open()) {
        // Save last 1000 entries
        size_t start = (history_.size() > 1000) ? history_.size() - 1000 : 0;
        for (size_t i = start; i < history_.size(); ++i) {
            file << history_[i] << "\n";
        }
        file.close();
        LOG_DEBUG("Saved {} history entries", history_.size() - start);
    }
}

void InputHandler::AddToHistory(const std::string& line) {
    if (line.empty()) return;

    // Don't add duplicate of last entry
    if (!history_.empty() && history_.back() == line) {
        return;
    }

    history_.push_back(line);
    history_index_ = history_.size();

    // Limit history size
    if (history_.size() > 1000) {
        history_.erase(history_.begin());
    }
}

std::string InputHandler::ReadLine(const std::string& prompt) {
    EnableRawMode();

    buffer_.clear();
    cursor_pos_ = 0;
    completions_.clear();
    completion_index_ = 0;

    std::cout << prompt << std::flush;

    while (true) {
        int c = ReadChar();

        if (c == -1) {
            continue;
        }

        // Ctrl+C or Ctrl+D
        if (c == 3 || c == 4) {
            std::cout << "\n" << std::flush;
            DisableRawMode();
            return (c == 4) ? "" : std::string(1, c);
        }

        // Enter
        if (c == 13 || c == 10) {
            std::cout << "\n" << std::flush;
            DisableRawMode();
            return buffer_;
        }

        // Backspace
        if (c == 127 || c == 8) {
            HandleBackspace();
            RefreshLine(prompt);
            continue;
        }

        // Delete
        if (c == 126) {
            HandleDelete();
            RefreshLine(prompt);
            continue;
        }

        // Escape sequence
        if (c == 27) {
            int c2 = ReadCharWithTimeout(10);
            int c3 = ReadCharWithTimeout(10);

            if (c2 == -1) {
                // Alt key or escape
                continue;
            }

            if (c2 == 91) {  // CSI sequence
                if (c3 == 65) {  // Up arrow
                    HandleHistoryUp();
                    RefreshLine(prompt);
                } else if (c3 == 66) {  // Down arrow
                    HandleHistoryDown();
                    RefreshLine(prompt);
                } else if (c3 == 67) {  // Right arrow
                    HandleRight();
                    RefreshLine(prompt);
                } else if (c3 == 68) {  // Left arrow
                    HandleLeft();
                    RefreshLine(prompt);
                } else if (c3 == 72) {  // Home
                    HandleHome();
                    RefreshLine(prompt);
                } else if (c3 == 70) {  // End
                    HandleEnd();
                    RefreshLine(prompt);
                } else if (c3 == 51) {  // Delete (needs one more char)
                    ReadCharWithTimeout(10);  // Consume '~'
                    HandleDelete();
                    RefreshLine(prompt);
                }
            }
            continue;
        }

        // Tab
        if (c == 9) {
            HandleTab();
            RefreshLine(prompt);
            continue;
        }

        // Regular character
        if (c >= 32 && c <= 126) {
            buffer_.insert(buffer_.begin() + cursor_pos_, static_cast<char>(c));
            cursor_pos_++;
            completions_.clear();
            RefreshLine(prompt);
            continue;
        }
    }
}

void InputHandler::HandleTab() {
    if (!completion_callback_) return;

    // If we already have completions, cycle through them
    if (!completions_.empty()) {
        completion_index_ = (completion_index_ + 1) % completions_.size();
        buffer_ = completion_prefix_ + completions_[completion_index_];
        cursor_pos_ = buffer_.size();
        return;
    }

    // Save original buffer for cycling
    original_buffer_ = buffer_;

    // Get new completions
    completions_ = completion_callback_(buffer_, cursor_pos_);

    if (completions_.empty()) {
        return;  // No completions
    }

    if (completions_.size() == 1) {
        // Single completion, apply it
        buffer_ = completion_prefix_ + completions_[0];
        cursor_pos_ = buffer_.size();
        completions_.clear();
        return;
    }

    // Multiple completions - find and apply common prefix first
    std::string common = FindCommonPrefix(completions_);
    if (!common.empty() && common != completion_prefix_) {
        // Apply common prefix, don't cycle yet
        buffer_ = completion_prefix_ + common;
        cursor_pos_ = buffer_.size();
        // Keep completions for next tab to cycle
        completion_index_ = 0;
        return;
    }

    // No common prefix to apply, show completions on second tab
    completion_prefix_ = buffer_;
    completion_index_ = 0;
    ShowCompletions();
}

std::string InputHandler::FindCommonPrefix(const std::vector<std::string>& completions) {
    if (completions.empty()) return "";
    if (completions.size() == 1) return completions[0];

    std::string prefix = completions[0];
    for (size_t i = 1; i < completions.size(); ++i) {
        const std::string& s = completions[i];
        size_t len = std::min(prefix.size(), s.size());
        size_t j = 0;
        while (j < len && prefix[j] == s[j]) {
            j++;
        }
        prefix = prefix.substr(0, j);
        if (prefix.empty()) break;
    }
    return prefix;
}

void InputHandler::ShowCompletions() {
    if (completions_.empty()) return;

    // Save cursor position and move to new line
    std::cout << "\r\n" << ansi::kSaveCursor;

    // Calculate column layout
    const size_t max_count = std::min(completions_.size(), size_t(20));
    const size_t cols = 4;  // Display in 4 columns
    const size_t rows = (max_count + cols - 1) / cols;

    // Find max width for each column
    std::vector<size_t> col_widths(cols, 0);
    for (size_t i = 0; i < max_count; ++i) {
        size_t col = i / rows;
        col_widths[col] = std::max(col_widths[col], completions_[i].size());
    }

    // Display in column layout
    for (size_t row = 0; row < rows; ++row) {
        std::cout << "  ";
        for (size_t col = 0; col < cols; ++col) {
            size_t idx = col * rows + row;
            if (idx < max_count) {
                std::cout << ansi::Color(36) << completions_[idx];  // Cyan color
                // Add padding to next column
                if (col < cols - 1 && idx + rows < max_count) {
                    size_t padding = col_widths[col] - completions_[idx].size() + 4;
                    for (size_t p = 0; p < padding; ++p) {
                        std::cout << ' ';
                    }
                }
            }
        }
        std::cout << "\n";
    }

    if (completions_.size() > 20) {
        std::cout << ansi::kDim << "  ... and " << (completions_.size() - 20) << " more\n";
    }

    // Restore cursor position and clear to end of line
    std::cout << ansi::kRestoreCursor << ansi::kClearLine << std::flush;
}

void InputHandler::RefreshLine(const std::string& prompt) {
    // Move cursor to beginning of line
    std::cout << "\r" << ansi::kClearLine << prompt << buffer_;

    // Move cursor back to position
    if (cursor_pos_ < buffer_.size()) {
        std::cout << ansi::MoveCursorLeft(buffer_.size() - cursor_pos_);
    }

    std::cout << std::flush;
}

void InputHandler::ClearLine() {
    std::cout << "\r" << ansi::kClearLine << std::flush;
}

void InputHandler::MoveCursor(int columns) {
    if (columns > 0) {
        std::cout << ansi::MoveCursorRight(columns) << std::flush;
    } else if (columns < 0) {
        std::cout << ansi::MoveCursorLeft(-columns) << std::flush;
    }
}

void InputHandler::HandleHistoryUp() {
    if (history_.empty() || history_index_ == 0) return;
    history_index_--;
    buffer_ = history_[history_index_];
    cursor_pos_ = buffer_.size();
}

void InputHandler::HandleHistoryDown() {
    if (history_index_ >= history_.size()) return;
    history_index_++;
    if (history_index_ >= history_.size()) {
        buffer_.clear();
        cursor_pos_ = 0;
    } else {
        buffer_ = history_[history_index_];
        cursor_pos_ = buffer_.size();
    }
}

void InputHandler::HandleLeft() {
    if (cursor_pos_ > 0) {
        cursor_pos_--;
    }
}

void InputHandler::HandleRight() {
    if (cursor_pos_ < buffer_.size()) {
        cursor_pos_++;
    }
}

void InputHandler::HandleHome() {
    cursor_pos_ = 0;
}

void InputHandler::HandleEnd() {
    cursor_pos_ = buffer_.size();
}

void InputHandler::HandleDelete() {
    if (cursor_pos_ < buffer_.size()) {
        buffer_.erase(buffer_.begin() + cursor_pos_, buffer_.begin() + cursor_pos_ + 1);
    }
}

void InputHandler::HandleBackspace() {
    if (cursor_pos_ > 0) {
        buffer_.erase(buffer_.begin() + cursor_pos_ - 1, buffer_.begin() + cursor_pos_);
        cursor_pos_--;
    }
}

bool InputHandler::IsInputComplete(const std::string& input) const {
    // Check for unclosed quotes
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (c == '\\' && i + 1 < input.size()) {
            i++;  // Skip escaped character
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        }
    }

    // If quotes are unclosed, input is not complete
    if (in_single_quote || in_double_quote) {
        return false;
    }

    // Check for trailing backslash (continuation)
    if (!input.empty() && input.back() == '\\') {
        return false;
    }

    return true;
}

}  // namespace aicode
