// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace aicode {

/// BackgroundRunTool - Execute commands in background
class BackgroundRunTool {
public:
    static BackgroundRunTool& GetInstance();

    /// Execute background operations
    std::string Execute(const std::string& action, const nlohmann::json& params);

private:
    BackgroundRunTool() = default;

    std::string Run(const nlohmann::json& params);
    std::string Get(const nlohmann::json& params);
    std::string List(const nlohmann::json& params);
    std::string Cancel(const nlohmann::json& params);
    std::string Drain(const nlohmann::json& params);
};

}  // namespace aicode
