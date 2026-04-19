// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/agent_state_observer.h"

#include <algorithm>
#include <cmath>

#include "media_core.h"
#include "drawer.h"
#include "colors.h"
#include "scene/ui_renderer.h"
#include "common/log_wrapper.h"

namespace aicode {

// ============================================================================
// AgentStateNotifier Implementation
// ============================================================================

AgentStateNotifier& AgentStateNotifier::GetInstance() {
    static AgentStateNotifier instance;
    return instance;
}

void AgentStateNotifier::AddObserver(std::weak_ptr<AgentStateObserver> observer) {
    std::lock_guard<std::mutex> lock(mutex_);
    observers_.push_back(observer);
}

void AgentStateNotifier::RemoveObserver(std::weak_ptr<AgentStateObserver> observer) {
    std::lock_guard<std::mutex> lock(mutex_);
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
            [&observer](const std::weak_ptr<AgentStateObserver>& wp) {
                return observer.owner_before(wp) && wp.owner_before(observer);
            }),
        observers_.end());
}

void AgentStateNotifier::NotifyStateChanged(const std::string& session_id,
                                             const std::string& role_id,
                                             AgentRuntimeState new_state,
                                             const std::string& details) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_state_ = new_state;

    // Notify all observers
    for (auto& weak_observer : observers_) {
        if (auto observer = weak_observer.lock()) {
            observer->OnAgentStateChanged(session_id, role_id, new_state, details);
        }
    }
}

AgentRuntimeState AgentStateNotifier::GetCurrentState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_state_;
}

// ============================================================================
// AgentStateVisualizer Implementation
// ============================================================================

AgentStateVisualizer& AgentStateVisualizer::GetInstance() {
    static AgentStateVisualizer instance;
    return instance;
}

void AgentStateVisualizer::Initialize() {
    LOG_INFO("AgentStateVisualizer initialized.");
}

void AgentStateVisualizer::Update(float delta_time) {
    animation_time_ += delta_time;
    UpdateFloatingPosition(animation_time_);
}

void AgentStateVisualizer::UpdateFloatingPosition(float time) {
    // Gentle floating animation using sine waves
    float base_x = 150.0f;  // Base position from left
    float base_y = 150.0f;  // Base position from top

    // Circular floating motion
    float_offset_x_ = std::sin(time * 2.0f) * 20.0f;  // 20px horizontal sway
    float_offset_y_ = std::cos(time * 1.5f) * 15.0f;  // 15px vertical sway

    float_x_ = base_x + float_offset_x_;
    float_y_ = base_y + float_offset_y_;

    // Pulse effect for certain states
    auto props = GetStateVisualProps(agent_state_);
    if (agent_state_ == AgentRuntimeState::THINKING ||
        agent_state_ == AgentRuntimeState::EXECUTING_TOOL) {
        // Pulse alpha between 0.7 and 1.0
        pulse_alpha_ = 0.85f + 0.15f * std::sin(time * 8.0f);
    } else {
        pulse_alpha_ = 1.0f;
    }
}

void AgentStateVisualizer::Render() {
    if (!visible_) return;

    DrawCapybara();
}

void AgentStateVisualizer::DrawCapybara() {
    auto props = GetStateVisualProps(agent_state_);

    // Calculate alpha from pulse
    uint8_t alpha = static_cast<uint8_t>(255 * pulse_alpha_);

    // Capybara colors based on state
    uint8_t fur_r = 139;  // Brown fur base
    uint8_t fur_g = 90;
    uint8_t fur_b = 43;

    if (agent_state_ == AgentRuntimeState::THINKING ||
        agent_state_ == AgentRuntimeState::EXECUTING_TOOL) {
        // Pulse effect - lighter when active
        fur_r = 160;
        fur_g = 110;
        fur_b = 60;
    }

    float capy_x = float_x_;
    float capy_y = float_y_;
    float scale = 1.0f;

    // ===== Body (oval, sitting posture) =====
    ::Drawer::Instance().DrawFillEllipse(
        capy_x, capy_y + 35 * scale,
        35 * scale, 30 * scale,
        Color(fur_r, fur_g, fur_b, alpha));

    // ===== Head (rounded square shape - capybara's distinctive face) =====
    float head_x = capy_x;
    float head_y = capy_y - 25 * scale;
    float head_size = 28 * scale;

    // Main head shape
    ::Drawer::Instance().DrawFillRect(
        head_x - head_size, head_y - head_size * 0.8f,
        head_size * 2, head_size * 1.6f,
        Color(fur_r, fur_g, fur_b, alpha));

    // Rounded corners (simulate with circles)
    ::Drawer::Instance().DrawFillCircle(head_x - head_size, head_y - head_size * 0.8f, 8 * scale, Color(fur_r, fur_g, fur_b, alpha));
    ::Drawer::Instance().DrawFillCircle(head_x + head_size, head_y - head_size * 0.8f, 8 * scale, Color(fur_r, fur_g, fur_b, alpha));
    ::Drawer::Instance().DrawFillCircle(head_x - head_size, head_y + head_size * 0.8f, 8 * scale, Color(fur_r, fur_g, fur_b, alpha));
    ::Drawer::Instance().DrawFillCircle(head_x + head_size, head_y + head_size * 0.8f, 8 * scale, Color(fur_r, fur_g, fur_b, alpha));

    // ===== Ears (small rounded triangles on top) =====
    ::Drawer::Instance().DrawFillCircle(head_x - 15 * scale, head_y - head_size * 0.9f, 6 * scale, Color(fur_r - 20, fur_g - 20, fur_b - 20, alpha));
    ::Drawer::Instance().DrawFillCircle(head_x + 15 * scale, head_y - head_size * 0.9f, 6 * scale, Color(fur_r - 20, fur_g - 20, fur_b - 20, alpha));

    // ===== Snout (prominent feature of capybara) =====
    float snout_y = head_y + 8 * scale;
    ::Drawer::Instance().DrawFillEllipse(
        head_x, snout_y,
        12 * scale, 8 * scale,
        Color(fur_r + 30, fur_g + 20, fur_b + 10, alpha));

    // Nostrils
    ::Drawer::Instance().DrawFillCircle(head_x - 4 * scale, snout_y, 2 * scale, Color(50, 30, 20, alpha));
    ::Drawer::Instance().DrawFillCircle(head_x + 4 * scale, snout_y, 2 * scale, Color(50, 30, 20, alpha));

    // ===== Eyes (small, calm expression - capybara's zen look) =====
    float eye_y = head_y - 5 * scale;
    // Left eye
    ::Drawer::Instance().DrawFillCircle(head_x - 10 * scale, eye_y, 4 * scale, Color(30, 20, 10, alpha));
    // Right eye
    ::Drawer::Instance().DrawFillCircle(head_x + 10 * scale, eye_y, 4 * scale, Color(30, 20, 10, alpha));

    // Eye highlight (zen, relaxed eyes)
    ::Drawer::Instance().DrawFillCircle(head_x - 10 * scale, eye_y, 1.5f * scale, Color(255, 255, 255, alpha));
    ::Drawer::Instance().DrawFillCircle(head_x + 10 * scale, eye_y, 1.5f * scale, Color(255, 255, 255, alpha));

    // ===== State color indicator (scarf/collar) =====
    float scarf_y = capy_y + 15 * scale;
    ::Drawer::Instance().DrawFillRect(
        capy_x - 20 * scale, scarf_y - 5 * scale,
        40 * scale, 10 * scale,
        Color(props.r, props.g, props.b, alpha));

    // ===== Status text below =====
    float name_y = capy_y + 70 * scale;

    // Render state name using encapsulated SDL text rendering
    UIRenderer::Instance().RenderFloatingText(
        props.name, float_x_ - 30, name_y, 204, 204, 204, pulse_alpha_);

    // Draw details text (if any) below name
    if (!state_details_.empty()) {
        float details_y = name_y + 18.0f;
        UIRenderer::Instance().RenderFloatingText(
            state_details_.substr(0, 25), float_x_ - 40, details_y,
            153, 153, 153, pulse_alpha_ * 0.8f);
    }
}

void AgentStateVisualizer::SetAgentState(AgentRuntimeState state, const std::string& details) {
    agent_state_ = state;
    state_details_ = details;
}

void AgentStateVisualizer::SetVisible(bool visible) {
    visible_ = visible;
}

}  // namespace aicode
