// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <filesystem>

namespace prosophor {

// ============================================================================
// IM 多用户身份净化与路径安全(ADR-IM9)
// ============================================================================

/// 主动触发会话的保留 owner：与本地单用户、Web 用户/群隔离。
/// 来自 active/ 插件的会话统一使用该 owner（日志 sessions/{role}/active/，
/// 记忆 memory/users/active/），并需要从空闲检测/历史列表中排除。
inline constexpr const char* kActiveOwner = "active";

inline bool IsActiveOwner(const std::string& owner) {
    return owner == kActiveOwner;
}

/// 将外部输入(用户 ID/群 ID,如飞书 ou_xxx/oc_xxx)净化为安全的目录名。
/// 规则:小写 + 白名单字符 [a-z0-9_-] + 长度 ≤ 64;非法返回 nullopt。
/// 所有记忆目录路径都必须经过净化后再拼接,防止路径注入。
inline std::optional<std::string> SanitizeIdentity(const std::string& raw) {
    if (raw.empty() || raw.size() > 64) return std::nullopt;
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out += static_cast<char>(c);
        } else if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            return std::nullopt;
        }
    }
    return out;
}

/// 校验 candidate 是否位于 dir 之内(weakly_canonical 前缀匹配),拒绝 ../ 逃逸。
inline bool IsPathWithin(const std::filesystem::path& dir,
                         const std::filesystem::path& candidate) {
    auto base = std::filesystem::weakly_canonical(dir);
    auto full = std::filesystem::weakly_canonical(candidate);
    auto mismatch = std::mismatch(base.begin(), base.end(), full.begin(), full.end());
    return mismatch.first == base.end();
}

}  // namespace prosophor
