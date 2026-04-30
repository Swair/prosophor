// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>
#include <unordered_map>

namespace prosophor {

inline void InitLog(const std::string& level = "info") {
    static const std::unordered_map<std::string, spdlog::level::level_enum> kLevelMap = {
        {"trace", spdlog::level::trace},
        {"debug", spdlog::level::debug},
        {"info", spdlog::level::info},
        {"warn", spdlog::level::warn},
        {"warning", spdlog::level::warn},
        {"error", spdlog::level::err},
        {"critical", spdlog::level::critical}
    };

    auto it = kLevelMap.find(level);
    spdlog::level::level_enum log_level = (it != kLevelMap.end()) ? it->second : spdlog::level::info;
    spdlog::set_level(log_level);

    spdlog::debug("Log level set to: {}", level);
}

}  // namespace prosophor

#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_FATAL(...) spdlog::error(__VA_ARGS__)

