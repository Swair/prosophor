// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "components/ui_types.h"
#include <string>

namespace aicode {

/// Get visual properties for a given agent state
inline StateVisualProps GetStateVisualProps(AgentRuntimeState state) {
    switch (state) {
        case AgentRuntimeState::IDLE:
            return StateVisualProps{100, 100, 100, 255, "Idle"};
        case AgentRuntimeState::THINKING:
            return StateVisualProps{65, 105, 225, 255, "Thinking"};  // Royal Blue
        case AgentRuntimeState::EXECUTING_TOOL:
            return StateVisualProps{255, 165, 0, 255, "Executing"};  // Orange
        case AgentRuntimeState::WAITING_PERMISSION:
            return StateVisualProps{255, 255, 0, 255, "Waiting"};  // Yellow
        case AgentRuntimeState::STATE_ERROR:
            return StateVisualProps{255, 0, 0, 255, "Error"};  // Red
        case AgentRuntimeState::COMPLETE:
            return StateVisualProps{0, 255, 0, 255, "Complete"};  // Green
        default:
            return StateVisualProps{128, 128, 128, 255, "Unknown"};
    }
}

}  // namespace aicode
