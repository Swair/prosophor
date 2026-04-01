// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/system_prompt.h"

#include <sstream>

#include "common/log_wrapper.h"
#include "core/skill_loader.h"
#include "managers/memory_manager.h"
#include "common/config.h"

namespace aicode {

std::vector<SystemSchema> BuildSystemPrompt(
    const AiCodeConfig& config,
    std::shared_ptr<SkillLoader> skill_loader,
    std::shared_ptr<MemoryManager> memory_manager,
    const std::string& workspace_path) {

    std::vector<SystemSchema> schemas;

    // 1. Base identity prompt (cacheable)
    std::string base_prompt = "You are AiCode, an AI coding assistant.\n\n";
    base_prompt += "Think step by step. When you need to perform a task, use the available tools.\n";
    schemas.push_back({"text", base_prompt, true});

    // 2. Skills context (cacheable)
    auto skills = skill_loader->LoadSkills(config.skills, workspace_path);
    std::string skill_context = skill_loader->GetSkillContext(skills);
    if (!skill_context.empty()) {
        schemas.push_back({"text", skill_context, true});
    }

    // 3. Agent instructions from AGENTS.md (cacheable)
    std::string agents_content = memory_manager->ReadAgentsFile();
    if (!agents_content.empty()) {
        std::ostringstream agents_prompt;
        agents_prompt << "## Agent Instructions\n";
        agents_prompt << agents_content << "\n";
        schemas.push_back({"text", agents_prompt.str(), true});
    }

    // 4. Default behavior if no skills loaded (not cacheable, may change)
    if (skill_context.empty()) {
        std::ostringstream default_prompt;
        default_prompt << "You help users with coding tasks. You can:\n";
        default_prompt << "- Read and write files\n";
        default_prompt << "- Execute shell commands\n";
        default_prompt << "- Search for files and content\n";
        default_prompt << "- Use web search for research\n";
        schemas.push_back({"text", default_prompt.str(), false});
    }

    return schemas;
}

}  // namespace aicode
