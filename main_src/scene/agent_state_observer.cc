// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/agent_state_observer.h"
#include "scene/anime_character.h"
#include "scene/ui_renderer.h"
#include "scene/layout_config.h"

#include <algorithm>
#include <cmath>

#include "media_engine/media_engine.h"
#include "common/log_wrapper.h"

namespace prosophor {

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
}

void AgentStateVisualizer::Render() {
    if (!visible_) return;

    DrawBlackboard();
    DrawVirtualHumanCharacter();
}

void AgentStateVisualizer::DrawBlackboard() {
    int win_w = MediaCore::Instance().GetWindowWidth();
    int win_h = MediaCore::Instance().GetWindowHeight();

    float board_w = win_w * 0.63f;
    float board_h = win_h * 0.94f;
    float board_x = board_w * 0.01f;
    float board_y = board_h * 0.03f;

    // 木质外框
    float frame_thick = 12.0f;
    Color wood(139, 105, 48, 255);
    ::Drawer::Instance().DrawFillRect(board_x - frame_thick, board_y - frame_thick,
                                       board_w + frame_thick * 2, frame_thick, wood);
    ::Drawer::Instance().DrawFillRect(board_x - frame_thick, board_y + board_h + frame_thick,
                                       board_w + frame_thick * 2, frame_thick, wood);
    ::Drawer::Instance().DrawFillRect(board_x - frame_thick, board_y,
                                       frame_thick, board_h, wood);
    ::Drawer::Instance().DrawFillRect(board_x + board_w, board_y,
                                       frame_thick, board_h, wood);

    // 黑板面（深绿到墨绿渐变 - 用多行矩形模拟）
    Color board_top(45, 74, 45, 255);
    Color board_bot(26, 58, 26, 255);
    int steps = 10;
    for (int i = 0; i < steps; i++) {
        float t = static_cast<float>(i) / steps;
        uint8_t r = static_cast<uint8_t>(board_top.r + (board_bot.r - board_top.r) * t);
        uint8_t g = static_cast<uint8_t>(board_top.g + (board_bot.g - board_top.g) * t);
        uint8_t b = static_cast<uint8_t>(board_top.b + (board_bot.b - board_top.b) * t);
        float y = board_y + board_h * t;
        float h = board_h / steps + 1;
        ::Drawer::Instance().DrawFillRect(board_x, y, board_w, h, Color(r, g, b, 255));
    }

    // 粉笔字装饰
    Color chalk(255, 255, 240, 80);
    float text_y = board_y + board_h * 0.12f;
    std::string status_text = "AI AGENT";
    UIRenderer::Instance().RenderFloatingText(
        status_text, board_x + 30, text_y, 255, 255, 240, 0.25f);

    // 右下角粉笔槽
    float chalk_tray_x = board_x + board_w - 120;
    float chalk_tray_y = board_y + board_h - 15;
    ::Drawer::Instance().DrawFillRect(chalk_tray_x, chalk_tray_y, 100, 8,
                                       Color(160, 120, 60, 200));
    ::Drawer::Instance().DrawFillRect(chalk_tray_x + 10, chalk_tray_y + 1,
                                       12, 6, Color(255, 255, 255, 180));
}

void AgentStateVisualizer::DrawVirtualHumanCharacter() {
    int win_w = MediaCore::Instance().GetWindowWidth();
    int win_h = MediaCore::Instance().GetWindowHeight();

    // 角色位置：左侧 65% 区域居中
    float board_w = win_w * 0.63f;
    float board_h = win_h * 0.94f;
    float board_x = board_w * 0.01f;
    float board_y = board_h * 0.03f;

    float base_x = board_x + board_w / 2.0f;
    float base_y = board_y + board_h * 0.72f;  // 脚部位置

    // 呼吸动画
    float breathe_y = std::sin(animation_time_ * 3.14f) * 6.0f;  // ±6px
    float breathe_s = 1.0f + std::sin(animation_time_ * 3.14f) * 0.01f;  // ±1%

    // 眨眼动画：周期 4s，闭合 0.12s
    float blink_period = 4.0f;
    float blink_close = 0.12f;
    float blink_phase = std::fmod(animation_time_, blink_period);
    bool is_blinking = (blink_phase > blink_period - blink_close);

    // 状态颜色
    auto props = GetStateVisualProps(agent_state_);
    Color scarf_color(props.r, props.g, props.b);

    // 脉冲效果
    float pulse_alpha = 1.0f;
    if (agent_state_ == AgentRuntimeState::THINKING ||
        agent_state_ == AgentRuntimeState::EXECUTING_TOOL) {
        pulse_alpha = 0.88f + 0.12f * std::sin(animation_time_ * 8.0f);
    }

    float scale = 2.5f * breathe_s;

    AnimeCharacterRenderer::Instance().Render(
        character_type_, base_x, base_y + breathe_y, agent_state_, scarf_color, scale, pulse_alpha, is_blinking);

    // 状态名称文本
    float name_y = base_y + 80 * scale;
    UIRenderer::Instance().RenderFloatingText(
        props.name, base_x - 30, name_y, 204, 204, 204, pulse_alpha);

    // 详情文本
    if (!state_details_.empty()) {
        float details_y = name_y + 18.0f;
        UIRenderer::Instance().RenderFloatingText(
            state_details_.substr(0, 25), base_x - 40, details_y,
            153, 153, 153, pulse_alpha * 0.8f);
    }
}

void AgentStateVisualizer::SetAgentState(AgentRuntimeState state, const std::string& details) {
    agent_state_ = state;
    state_details_ = details;
}

void AgentStateVisualizer::SetVisible(bool visible) {
    visible_ = visible;
}

void AgentStateVisualizer::SetCharacterType(AnimeCharacterType type) {
    character_type_ = type;
}

}  // namespace prosophor
