// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <memory>

#include "common/config.h"
#include "core/messages_schema.h"

namespace aicode {

// Forward declarations
class SkillLoader;
class MemoryManager;

/// System message content block
struct SystemSchema {
    std::string type = "text";
    std::string text;
    bool cache_control = false;  // Whether to cache this system message
};

/// AgentState: holds all mutable state for an agent session
/// This struct is passed to AgentCore methods to keep AgentCore stateless
struct AgentState {
    // === Provider selection (per-user/session) ===
    std::string provider_name = "anthropic";

    // === Model config (per-user/session) ===
    std::string model = "claude-sonnet-4-6";
    double temperature = 0.7;
    int max_tokens = 8192;
    bool use_tools = true;

    // === Agent behavior (per-user/session) ===
    int max_iterations = 15;  // Dynamic max iterations for tool use loops

    // === Conversation history (per-user/session) ===
    std::vector<MessageSchema> messages;
    std::vector<SystemSchema> system_prompt;
};

/// Build system prompt for AgentCore
/// Combines skill context, agent instructions, and default behavior
std::vector<SystemSchema> BuildSystemPrompt(
    const AiCodeConfig& config,
    std::shared_ptr<SkillLoader> skill_loader,
    std::shared_ptr<MemoryManager> memory_manager,
    const std::string& workspace_path);

}  // namespace aicode
