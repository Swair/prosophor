// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32

#include "cli/input_handler.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <windows.h>
#include <conio.h>
#include <string>

#include "common/log_wrapper.h"

namespace {

// Convert UTF-16 wide string to UTF-8
std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";

    // Calculate required buffer size
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";

    std::string result(size - 1, 0);  // -1 to exclude null terminator
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Read a wide character from console, return UTF-8 string
std::string ReadWideChar() {
    INPUT_RECORD input_record;
    DWORD events;
    HANDLE h_input = GetStdHandle(STD_INPUT_HANDLE);

    while (true) {
        if (!ReadConsoleInputW(h_input, &input_record, 1, &events)) {
            return "";
        }

        if (input_record.EventType == KEY_EVENT && input_record.Event.KeyEvent.bKeyDown) {
            WORD vk = input_record.Event.KeyEvent.wVirtualKeyCode;
            DWORD state = input_record.Event.KeyEvent.dwControlKeyState;

            // Handle Ctrl+C
            if ((state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && vk == 'C') {
                return std::string(1, 3);  // Ctrl+C
            }

            // Handle Ctrl+D (EOF)
            if ((state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && vk == 'D') {
                return std::string(1, 4);  // Ctrl+D
            }

            // Get Unicode character from uChar.UnicodeChar
            wchar_t wc = input_record.Event.KeyEvent.uChar.UnicodeChar;
            if (wc != 0) {
                // Convert wide char to UTF-8
                std::wstring wide(1, wc);
                return WideToUtf8(wide);
            }

            // Handle special keys - return empty string, caller should check virtual key
            switch (vk) {
                case VK_UP:    return "\x1b[A";   // Escape sequence for up arrow
                case VK_DOWN:  return "\x1b[B";   // Down arrow
                case VK_LEFT:  return "\x1b[D";   // Left arrow
                case VK_RIGHT: return "\x1b[C";   // Right arrow
                case VK_HOME:  return "\x1b[H";   // Home
                case VK_END:   return "\x1b[F";   // End
                case VK_DELETE: return "\x1b[3~"; // Delete
                case VK_BACK:  return std::string(1, 127);  // Backspace
                case VK_RETURN: return std::string(1, 13);  // Enter
                case VK_ESCAPE: return std::string(1, 27);  // Escape
                case VK_TAB:   return std::string(1, 9);    // Tab
            }
        }
    }
}

}  // namespace

namespace aicode {

// Console state storage
static DWORD orig_console_mode = 0;
static bool console_mode_saved = false;
static HANDLE h_input = INVALID_HANDLE_VALUE;

InputHandler::InputHandler() {
    // Set default history file
    const char* home = getenv("HOME");
    if (home) {
        history_file_ = std::string(home) + "/.aicode/history";
    } else {
        // On Windows, use USERPROFILE if available
        const char* profile = getenv("USERPROFILE");
        if (profile) {
            history_file_ = std::string(profile) + "/.aicode/history";
        }
    }
}

InputHandler::~InputHandler() {
    DisableRawMode();
}

void InputHandler::EnableRawMode() {
    if (raw_mode_enabled_) return;

    if (h_input == INVALID_HANDLE_VALUE) {
        h_input = GetStdHandle(STD_INPUT_HANDLE);
    }

    if (!console_mode_saved && h_input != INVALID_HANDLE_VALUE) {
        if (!GetConsoleMode(h_input, &orig_console_mode)) {
            LOG_ERROR("Failed to get console mode");
            return;
        }
        console_mode_saved = true;

        // Disable all input processing for raw mode
        DWORD mode = 0;
        mode |= ENABLE_WINDOW_INPUT;  // Keep window events
        SetConsoleMode(h_input, mode);

        raw_mode_enabled_ = true;
        LOG_DEBUG("Raw mode enabled (Win32)");
        return;
    }

    raw_mode_enabled_ = true;
}

void InputHandler::DisableRawMode() {
    if (!raw_mode_enabled_) return;

    if (console_mode_saved && h_input != INVALID_HANDLE_VALUE) {
        SetConsoleMode(h_input, orig_console_mode);
        console_mode_saved = false;
    }

    raw_mode_enabled_ = false;
    LOG_DEBUG("Raw mode disabled (Win32)");
}

int InputHandler::ReadChar() {
    HANDLE h_input = GetStdHandle(STD_INPUT_HANDLE);

    while (true) {
        INPUT_RECORD input_record;
        DWORD events;

        if (!ReadConsoleInputW(h_input, &input_record, 1, &events)) {
            return -1;
        }

        if (input_record.EventType == KEY_EVENT && input_record.Event.KeyEvent.bKeyDown) {
            WORD vk = input_record.Event.KeyEvent.wVirtualKeyCode;
            DWORD state = input_record.Event.KeyEvent.dwControlKeyState;

            // Handle Ctrl+C
            if ((state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && vk == 'C') {
                return 3;  // Ctrl+C
            }

            // Handle Ctrl+D (EOF)
            if ((state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && vk == 'D') {
                return 4;  // Ctrl+D
            }

            // Get Unicode character from uChar.UnicodeChar
            wchar_t wc = input_record.Event.KeyEvent.uChar.UnicodeChar;
            if (wc != 0) {
                // For characters in ASCII range, return directly
                if (wc < 128) {
                    return static_cast<int>(wc);
                }
                // For wide characters (Chinese, etc.), convert to UTF-8 and cache
                wide_char_buffer_ = WideToUtf8(std::wstring(1, wc));
                wide_char_index_ = 0;
                // Return a marker to indicate UTF-8 sequence follows
                return -300;  // Special marker for UTF-8 char
            }

            // Handle special keys
            switch (vk) {
                case VK_UP:    return -200;  // Special marker for up arrow
                case VK_DOWN:  return -201;  // Down arrow
                case VK_LEFT:  return -202;  // Left arrow
                case VK_RIGHT: return -203;  // Right arrow
                case VK_HOME:  return -204;  // Home
                case VK_END:   return -205;  // End
                case VK_INSERT: return -206; // Insert
                case VK_DELETE: return -207; // Delete
                case VK_PRIOR: return -208;  // Page Up
                case VK_NEXT:  return -209;  // Page Down
                case VK_BACK:  return 127;   // Backspace
                case VK_RETURN: return 13;   // Enter
                case VK_ESCAPE: return 27;   // Escape
                case VK_TAB:   return 9;     // Tab
            }
        }
    }
}

// Get next byte from cached UTF-8 sequence, or -1 if none
int InputHandler::GetUtf8Byte() {
    if (wide_char_index_ < wide_char_buffer_.size()) {
        return static_cast<unsigned char>(wide_char_buffer_[wide_char_index_++]);
    }
    return -1;
}

int InputHandler::ReadCharWithTimeout(int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= timeout_ms) {
            return -1;
        }

        INPUT_RECORD input_record;
        DWORD events;
        if (PeekConsoleInputA(h_input, &input_record, 1, &events) && events > 0) {
            return ReadChar();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
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
        std::string cmd = "mkdir \"" + dir + "\" 2>nul || mkdir \"" + dir + "\"";
        system(cmd.c_str());
    }

    std::ofstream file(history_file_);
    if (file.is_open()) {
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

    if (!history_.empty() && history_.back() == line) {
        return;
    }

    history_.push_back(line);
    history_index_ = history_.size();

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
    wide_char_buffer_.clear();
    wide_char_index_ = 0;

    // 输出上方分隔线
    std::cout << ansi::kDim << "────────────────────────────────────────────────────────────────────────────────────────────" << ansi::kReset << std::endl;

    // 初始显示带底色的提示符
    RefreshLine(prompt);

    while (true) {
        int c = ReadChar();

        // Handle UTF-8 multi-byte character marker
        if (c == -300) {
            // Read all bytes of the UTF-8 sequence
            while (true) {
                int byte = GetUtf8Byte();
                if (byte == -1) break;
                buffer_.insert(buffer_.begin() + cursor_pos_, static_cast<char>(byte));
                cursor_pos_++;
            }
            wide_char_buffer_.clear();
            wide_char_index_ = 0;
            completions_.clear();
            RefreshLine(prompt);
            continue;
        }

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
            // 下方分隔线
            std::cout << "\r" << ansi::kClearLine
                      << ansi::kDim << "────────────────────────────────────────────────────────────────────────────────────────────" << ansi::kReset
                      << std::endl;
            DisableRawMode();
            return buffer_;
        }

        // Backspace - handle UTF-8 multi-byte characters
        if (c == 127 || c == 8) {
            if (cursor_pos_ > 0) {
                // Find the start of the current UTF-8 character
                size_t pos = cursor_pos_ - 1;
                while (pos > 0 && (buffer_[pos] & 0xC0) == 0x80) {
                    pos--;
                }
                buffer_.erase(buffer_.begin() + pos, buffer_.begin() + cursor_pos_);
                cursor_pos_ = pos;
            }
            RefreshLine(prompt);
            continue;
        }

        // Delete
        if (c == 126) {
            if (cursor_pos_ < buffer_.size()) {
                // Find the end of the current UTF-8 character
                size_t pos = cursor_pos_;
                pos++;
                while (pos < buffer_.size() && (buffer_[pos] & 0xC0) == 0x80) {
                    pos++;
                }
                buffer_.erase(buffer_.begin() + cursor_pos_, buffer_.begin() + pos);
            }
            RefreshLine(prompt);
            continue;
        }

        // Escape sequence
        if (c == 27) {
            int c2 = ReadCharWithTimeout(10);
            int c3 = ReadCharWithTimeout(10);

            if (c2 == -1) {
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
                } else if (c3 == 51) {  // Delete
                    ReadCharWithTimeout(10);
                    HandleDelete();
                    RefreshLine(prompt);
                }
            }
            continue;
        }

        // Handle special key markers from Windows
        if (c <= -200) {
            int key_code = -c - 200;
            switch (key_code) {
                case 0: HandleHistoryUp(); RefreshLine(prompt); break;
                case 1: HandleHistoryDown(); RefreshLine(prompt); break;
                case 2: HandleLeft(); RefreshLine(prompt); break;
                case 3: HandleRight(); RefreshLine(prompt); break;
                case 4: HandleHome(); RefreshLine(prompt); break;
                case 5: HandleEnd(); RefreshLine(prompt); break;
            }
            continue;
        }

        // Tab
        if (c == 9) {
            HandleTab();
            RefreshLine(prompt);
            continue;
        }

        // Regular character (including UTF-8 continuation bytes)
        // Accept all bytes >= 32, including UTF-8 multi-byte sequences
        if (c >= 32) {
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

    if (!completions_.empty()) {
        completion_index_ = (completion_index_ + 1) % completions_.size();
        buffer_ = completion_prefix_ + completions_[completion_index_];
        cursor_pos_ = buffer_.size();
        return;
    }

    original_buffer_ = buffer_;
    completions_ = completion_callback_(buffer_, cursor_pos_);

    if (completions_.empty()) {
        return;
    }

    if (completions_.size() == 1) {
        buffer_ = completion_prefix_ + completions_[0];
        cursor_pos_ = buffer_.size();
        completions_.clear();
        return;
    }

    std::string common = FindCommonPrefix(completions_);
    if (!common.empty() && common != completion_prefix_) {
        buffer_ = completion_prefix_ + common;
        cursor_pos_ = buffer_.size();
        completion_index_ = 0;
        return;
    }

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

    std::cout << "\r\n" << ansi::kSaveCursor;

    const size_t max_count = std::min(completions_.size(), size_t(20));
    const size_t cols = 4;
    const size_t rows = (max_count + cols - 1) / cols;

    std::vector<size_t> col_widths(cols, 0);
    for (size_t i = 0; i < max_count; ++i) {
        size_t col = i / rows;
        col_widths[col] = std::max(col_widths[col], completions_[i].size());
    }

    for (size_t row = 0; row < rows; ++row) {
        std::cout << "  ";
        for (size_t col = 0; col < cols; ++col) {
            size_t idx = col * rows + row;
            if (idx < max_count) {
                std::cout << ansi::Color(36) << completions_[idx];
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

    std::cout << ansi::kRestoreCursor << ansi::kClearLine << std::flush;
}

void InputHandler::RefreshLine(const std::string& prompt) {
    // 移动到行首并清除
    std::cout << "\r" << ansi::kClearLine;

    // 输入行 - 只显示提示符和输入内容
    std::cout << ansi::kFgBrightYellow << "❯ " << ansi::kReset
              << ansi::kBgBrightBlack << ansi::kFgBrightWhite
              << prompt << buffer_
              << ansi::kReset;

    // 光标位置调整
    int prompt_len = 2;  // "❯ " 的长度
    int content_width = prompt_len + buffer_.length();
    if (cursor_pos_ < buffer_.size()) {
        std::cout << ansi::MoveCursorLeft(content_width - cursor_pos_);
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
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (c == '\\' && i + 1 < input.size()) {
            i++;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        }
    }

    if (in_single_quote || in_double_quote) {
        return false;
    }

    if (!input.empty() && input.back() == '\\') {
        return false;
    }

    return true;
}

}  // namespace aicode

#endif  // _WIN32
