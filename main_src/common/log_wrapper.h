// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <spdlog/spdlog.h>

namespace aicode {


}  // namespace aicode

#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
