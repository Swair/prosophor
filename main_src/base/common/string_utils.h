// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace prosophor {

// ============================================================================
// String Encoding Utilities
// ============================================================================

/// IsUtf8 - Check if a string is valid UTF-8 encoded
/// @param input String to check
/// @return true if valid UTF-8, false otherwise
bool IsUtf8(const std::string& input);

// ============================================================================
// Case Conversion
// ============================================================================

/// ToLower - ASCII 小写转换（非 ASCII 字节原样保留，UTF-8 多字节不受损）
/// @param text 待转换文本（按值传入，便于就地转换后返回）
/// @return 小写副本
std::string ToLower(std::string text);

/// ToUpper - ASCII 大写转换，语义同 ToLower
std::string ToUpper(std::string text);

/// EqualsIgnoreCase - ASCII 大小写无关比较
bool EqualsIgnoreCase(const std::string& lhs, const std::string& rhs);

}  // namespace prosophor
