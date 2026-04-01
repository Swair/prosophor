// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <memory>

namespace aicode {

class SkillLoader;
class MemoryManager;
struct AiCodeConfig;

/// System message content block
struct SystemSchema {
    std::string type = "text";
    std::string text;
    bool cache_control = false;  // Whether to cache this system message
};

/// Build system prompt for AgentCore
/// Combines skill context, agent instructions, and default behavior
std::vector<SystemSchema> BuildSystemPrompt(
    const AiCodeConfig& config,
    std::shared_ptr<SkillLoader> skill_loader,
    std::shared_ptr<MemoryManager> memory_manager,
    const std::string& workspace_path);

}  // namespace aicode
