// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/string_utils.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "platform/platform.h"

namespace prosophor {

std::string ToLower(std::string text) {
    // 必须经 unsigned char：直接传 char 给 std::tolower 对负值（非 ASCII 字节）是 UB
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string ToUpper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

bool EqualsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return lhs.size() == rhs.size() && ToLower(lhs) == ToLower(rhs);
}

bool IsUtf8(const std::string& input) {
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) continue;
        if (c >= 0xC2 && c <= 0xDF) {
            if (++i >= input.size() || (input[i] & 0xC0) != 0x80) return false;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 >= input.size() || (input[++i] & 0xC0) != 0x80 || (input[++i] & 0xC0) != 0x80) return false;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 >= input.size() || (input[++i] & 0xC0) != 0x80 || (input[++i] & 0xC0) != 0x80 || (input[++i] & 0xC0) != 0x80) return false;
        } else return false;
    }
    return true;
}

std::string ConvertToUtf8(const std::string& input) {
    return Platform::NativeToUtf8(input);
}

std::string ReadLine() {
    return Platform::ReadLine();
}

}  // namespace prosophor
