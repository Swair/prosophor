// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene/character_state_observer.h"
#include "scene/office_character_manager.h"
#include "common/log_wrapper.h"
#include <vector>

namespace aicode {

CharacterStateObserver& CharacterStateObserver::Instance() {
    static CharacterStateObserver instance;
    return instance;
}

void CharacterStateObserver::OnAgentStateChanged(const std::string& session_id,
                                                  const std::string& role_id,
                                                  AgentRuntimeState new_state,
                                                  const std::string& details) {
    (void)session_id;  // Unused for now

    LOG_DEBUG("Agent state changed: role={}, state={}, details={}",
              role_id, static_cast<int>(new_state), details);

    // Map agent state to character state
    CharacterState char_state = MapToCharacterState(new_state);

    // Get character and update
    auto& char_mgr = OfficeCharacterManager::Instance();

    if (new_state == AgentRuntimeState::EXECUTING_TOOL) {
        // Active with current tool
        char_mgr.SetCharacterActivity(role_id, true, current_tool_);
    } else if (new_state == AgentRuntimeState::THINKING) {
        // Thinking - typing animation
        char_mgr.SetCharacterActivity(role_id, true, "");
    } else if (new_state == AgentRuntimeState::WAITING_PERMISSION) {
        // Waiting for permission - idle with bubble (TODO: add bubble support)
        char_mgr.SetCharacterActivity(role_id, false, "");
    } else if (new_state == AgentRuntimeState::STATE_ERROR) {
        // Error - idle state
        char_mgr.SetCharacterActivity(role_id, false, "");
    } else if (new_state == AgentRuntimeState::COMPLETE) {
        // Complete - return to idle
        char_mgr.SetCharacterActivity(role_id, false, "");
    } else {
        // IDLE or unknown
        char_mgr.SetCharacterActivity(role_id, false, "");
    }

    // Also update the global state visualizer for backwards compatibility
    AgentStateNotifier::GetInstance().NotifyStateChanged(session_id, role_id, new_state, details);
}

void CharacterStateObserver::SetCurrentTool(const std::string& tool_name) {
    current_tool_ = tool_name;
    LOG_DEBUG("CharacterStateObserver: current tool set to '{}'", tool_name);
}

CharacterState CharacterStateObserver::MapToCharacterState(AgentRuntimeState state) {
    switch (state) {
        case AgentRuntimeState::THINKING:
            return CharacterState::TYPE;
        case AgentRuntimeState::EXECUTING_TOOL:
            return IsReadingTool(current_tool_) ? CharacterState::READ : CharacterState::TYPE;
        case AgentRuntimeState::WAITING_PERMISSION:
            return CharacterState::IDLE;
        case AgentRuntimeState::STATE_ERROR:
            return CharacterState::IDLE;
        case AgentRuntimeState::COMPLETE:
            return CharacterState::IDLE;
        case AgentRuntimeState::IDLE:
        default:
            return CharacterState::IDLE;
    }
}

bool CharacterStateObserver::IsReadingTool(const std::string& tool_name) const {
    // Reading tools show reading animation instead of typing
    static const std::vector<std::string> reading_tools = {
        "Read", "Grep", "Glob", "WebFetch", "WebSearch"
    };
    for (const auto& t : reading_tools) {
        if (tool_name == t) return true;
    }
    return false;
}

}  // namespace aicode
