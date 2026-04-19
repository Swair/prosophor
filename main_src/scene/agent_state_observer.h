// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"
#include "core/agent_state_visualizer.h"

#include <string>
#include <memory>
#include <mutex>
#include <functional>

namespace aicode {

/// AgentStateObserver: Callback interface for agent state changes
class AgentStateObserver {
 public:
    virtual ~AgentStateObserver() = default;
    virtual void OnAgentStateChanged(const std::string& session_id,
                                     const std::string& role_id,
                                     AgentRuntimeState new_state,
                                     const std::string& details) = 0;
};

/// AgentStateNotifier: Singleton for broadcasting state changes
/// Thread-safe observer pattern for agent state updates
class AgentStateNotifier : public Noncopyable {
 public:
    static AgentStateNotifier& GetInstance();

    /// Register an observer
    void AddObserver(std::weak_ptr<AgentStateObserver> observer);

    /// Remove an observer
    void RemoveObserver(std::weak_ptr<AgentStateObserver> observer);

    /// Notify all observers of state change
    void NotifyStateChanged(const std::string& session_id,
                            const std::string& role_id,
                            AgentRuntimeState new_state,
                            const std::string& details = "");

    /// Get current state
    AgentRuntimeState GetCurrentState() const;

 private:
    AgentStateNotifier() = default;

    mutable std::mutex mutex_;
    std::vector<std::weak_ptr<AgentStateObserver>> observers_;
    AgentRuntimeState current_state_ = AgentRuntimeState::IDLE;
};

/// AgentStateVisualizer: Renders agent state as SDL visual indicator
/// Draws a colored circle + icon + status text that follows mouse/floats
class AgentStateVisualizer : public Noncopyable {
 public:
    static AgentStateVisualizer& GetInstance();

    /// Initialize visual resources
    void Initialize();

    /// Render the state indicator
    /// Parameters control position and animation
    void Render();

    /// Update internal state (call from update loop)
    void Update(float delta_time);

    /// Set the current agent state to display
    void SetAgentState(AgentRuntimeState state, const std::string& details = "");

    /// Toggle visibility
    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }

    /// Get current agent state
    AgentRuntimeState GetAgentState() const { return agent_state_; }

 private:
    AgentStateVisualizer() = default;

    /// Calculate floating position based on time
    void UpdateFloatingPosition(float time);

    /// Draw the state indicator at current position
    void DrawIndicator();

    /// Draw the capybara sprite
    void DrawCapybara();

    // State
    AgentRuntimeState agent_state_ = AgentRuntimeState::IDLE;
    bool visible_ = true;
    float float_x_ = 100.0f;
    float float_y_ = 100.0f;
    float float_offset_x_ = 0.0f;
    float float_offset_y_ = 0.0f;
    float animation_time_ = 0.0f;
    float pulse_alpha_ = 1.0f;
    std::string state_details_;

    // Visual config
    float indicator_radius_ = 25.0f;
    float text_offset_ = 35.0f;
};

}  // namespace aicode
