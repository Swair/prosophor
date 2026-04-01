// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/log_wrapper.h"
#include "core/agent_commander.h"
#include "common/config.h"

#include <spdlog/spdlog.h>
#include <unordered_map>

static void InitLog() {
    const auto& config = aicode::AiCodeConfig::GetInstance();

    static const std::unordered_map<std::string, spdlog::level::level_enum> kLevelMap = {
        {"trace", spdlog::level::trace},
        {"debug", spdlog::level::debug},
        {"info", spdlog::level::info},
        {"warn", spdlog::level::warn},
        {"warning", spdlog::level::warn},
        {"error", spdlog::level::err},
        {"critical", spdlog::level::critical}
    };

    auto it = kLevelMap.find(config.log_level);
    spdlog::level::level_enum level = (it != kLevelMap.end()) ? it->second : spdlog::level::info;
    spdlog::set_level(level);

    LOG_INFO("Log level set to: {}", config.log_level);
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    InitLog();
    LOG_INFO("AiCode v{}", AICODE_VERSION);

    try {
        return aicode::AgentCommander::GetInstance().Run();
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: {}", e.what());
        return 1;
    }
}
