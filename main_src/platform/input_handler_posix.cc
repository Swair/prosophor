// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32

#include "platform/input_handler.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/log_wrapper.h"

namespace prosophor {

// Terminal state storage
static termios orig_termios;
static bool termios_saved = false;

InputHandler::InputHandler() {
    // Set default history file
    const char* home = getenv("HOME");
    if (home) {
        history_file_ = std::string(home) + "/.prosophor/history";
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
    unsigned char c;
    ssize_t result;
    while ((result = read(STDIN_FILENO, &c, 1)) != 1) {
        if (result == -1 && errno != EAGAIN) {
            return -1;
        }
    }
    return static_cast<int>(c);
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

// Helper function to check if a byte is a UTF-8 continuation byte (10xxxxxx)
static bool IsUtf8ContinuationByte(unsigned char c) {
    return (c & 0b11000000) == 0b10000000;
}

// Helper function to get the byte length of a UTF-8 character from its first byte
static int GetUtf8CharLen(unsigned char first_byte) {
    if ((first_byte & 0b10000000) == 0b00000000) return 1;  // 0xxxxxxx
    if ((first_byte & 0b11100000) == 0b11000000) return 2;  // 110xxxxx
    if ((first_byte & 0b11110000) == 0b11100000) return 3;  // 1110xxxx
    if ((first_byte & 0b11111000) == 0b11110000) return 4;  // 11110xxx
    return 1;  // Invalid or continuation byte, treat as single
}

// Helper function to move cursor backward by one UTF-8 character
void InputHandler::HandleBackspace() {
    if (cursor_pos_ <= 0) return;

    // Find the start of the previous UTF-8 character
    size_t pos = cursor_pos_ - 1;
    // Move back until we find the start of a UTF-8 character
    while (pos > 0 && IsUtf8ContinuationByte(static_cast<unsigned char>(buffer_[pos]))) {
        pos--;
    }

    // Calculate how many bytes to delete
    size_t char_len = cursor_pos_ - pos;
    buffer_.erase(pos, char_len);
    cursor_pos_ = pos;
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
        int ret = system(cmd.c_str());
        (void)ret;
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
    // Linux 平台：输出 prompt，使用标准输入读取
    // 终端会自动回显输入字符
    std::cout << prompt << std::flush;

    std::string line;
    std::getline(std::cin, line);

    // 处理 Ctrl+D (EOF)
    if (std::cin.eof()) {
        std::cin.clear();
        return "";
    }

    return line;
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

// Helper function to calculate display width of a string (for cursor positioning)
// ASCII characters = 1 column, CJK characters = 2 columns
static int GetDisplayWidth(const std::string& str) {
    int width = 0;
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        int char_len = GetUtf8CharLen(c);

        // Simple heuristic: ASCII = 1 column, non-ASCII (CJK, etc.) = 2 columns
        if (c < 128) {
            width += 1;
        } else {
            width += 2;  // CJK and other wide characters
        }
        i += char_len;
    }
    return width;
}


void InputHandler::RefreshLine(const std::string& prompt) {
    // Move cursor to beginning of line
    std::cout << "\r" << ansi::kClearLine << prompt << buffer_;

    // Calculate display width from byte offset 0 to cursor_pos_
    // cursor_pos_ is a byte offset, we need to calculate the display width up to that point
    size_t byte_offset = cursor_pos_;
    int display_width = GetDisplayWidth(buffer_.substr(0, byte_offset));
    int total_width = GetDisplayWidth(buffer_);

    // Move cursor back from end to the cursor position
    int chars_to_move = total_width - display_width;
    if (chars_to_move > 0) {
        std::cout << ansi::MoveCursorLeft(chars_to_move);
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
    if (cursor_pos_ == 0) return;
    // Move back to the start of the previous UTF-8 character
    size_t pos = cursor_pos_ - 1;
    while (pos > 0 && IsUtf8ContinuationByte(static_cast<unsigned char>(buffer_[pos]))) {
        pos--;
    }
    cursor_pos_ = pos;
}

void InputHandler::HandleRight() {
    if (cursor_pos_ >= buffer_.size()) return;
    // Move forward by one UTF-8 character
    int char_len = GetUtf8CharLen(static_cast<unsigned char>(buffer_[cursor_pos_]));
    cursor_pos_ += std::min(static_cast<size_t>(char_len), buffer_.size() - cursor_pos_);
}

void InputHandler::HandleHome() {
    cursor_pos_ = 0;
}

void InputHandler::HandleEnd() {
    cursor_pos_ = buffer_.size();
}

void InputHandler::HandleDelete() {
    if (cursor_pos_ >= buffer_.size()) return;
    // Delete the UTF-8 character at cursor position
    int char_len = GetUtf8CharLen(static_cast<unsigned char>(buffer_[cursor_pos_]));
    buffer_.erase(cursor_pos_, char_len);
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

}  // namespace prosophor

#endif  // _WIN32
