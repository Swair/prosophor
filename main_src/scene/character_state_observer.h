// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "scene/agent_state_observer.h"
#include "scene/character_sprite.h"  // For CharacterState
#include <memory>
#include <string>

namespace aicode {

// Forward declaration to avoid circular dependency
class OfficeCharacterManager;

/// CharacterStateObserver: Links AgentRuntimeState to CharacterState
/// Maps agent states to character animations for office scene
class CharacterStateObserver : public AgentStateObserver {
public:
    static CharacterStateObserver& Instance();

    void OnAgentStateChanged(const std::string& session_id,
                             const std::string& role_id,
                             AgentRuntimeState new_state,
                             const std::string& details) override;

    /// Set current tool for animation selection
    void SetCurrentTool(const std::string& tool_name);

private:
    CharacterStateObserver() = default;

    /// Map AgentRuntimeState to CharacterState
    CharacterState MapToCharacterState(AgentRuntimeState state);

    /// Map tool name to reading/typing animation
    bool IsReadingTool(const std::string& tool_name) const;

    std::string current_tool_;
};

}  // namespace aicode
