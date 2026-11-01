// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/sprite.h"
#include "virtual_sprite/sprite_manager.h"
#include "virtual_sprite/ui_renderer.h"
#include "virtual_sprite/layout_config.h"
#include "virtual_sprite/asset_define.h"
#include "virtual_sprite/components/speech_bubble.h"
#include "media_engine/media_engine.h"
#include "prosophor_core/public/agent_engine.h"
#include "common/log_wrapper.h"
#include "common/file_utils.h"
#include "common/time_wrapper.h"
#include "virtual_sprite/voice_engine.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "platform/platform.h"

namespace prosophor {


Sprite::Sprite(const std::string& name, int width, int height, const std::string& role_id)
    : name_(name), role_id_(role_id), width_(width), height_(height) {}

Sprite::~Sprite() = default;

bool Sprite::Create() {
    auto& mc = media_engine::MediaCore::Instance();

    // ── Create sprite window (always-on-top, never occluded) ──
    media_engine::WindowConfig cfg;
    cfg.transparent_bg = true;
    cfg.use_shared_font = true;
    cfg.borderless = true;
    cfg.resizable = false;
    cfg.transparent_window = true;
    cfg.skip_taskbar = true;
    cfg.always_on_top = true;
    sprite_window_ = mc.CreateMediaWindow(name_.c_str(), width_, height_, cfg);
    if (!sprite_window_) {
        LOG_ERROR("[Sprite] Failed to create window '{}'", name_);
        return false;
    }

    // ── Per-sprite SpeechBubble (云朵桩) — construct before widget tree ──
    speech_bubble_ = std::make_unique<SpeechBubble>();
    {
        auto layout = LayoutConfig{};
        speech_bubble_->SetMinBubbleSize(layout.bubble_min_width, layout.bubble_min_body_height);
        speech_bubble_->SetBubbleRadius(layout.bubble_radius);
        speech_bubble_->SetPadding(layout.bubble_padding);
        speech_bubble_->SetTailHeight(layout.bubble_tail_height);
        speech_bubble_->SetTitleHeight(layout.bubble_title_height);
        speech_bubble_->SetInputHeight(layout.bubble_input_height);
        speech_bubble_->SetButtonSize(layout.bubble_btn_size);
        speech_bubble_->SetInputCornerRadius(layout.bubble_radius);
    }

    // ── Widget tree root ──
    root_widget_.SetBackgroundColor(media_engine::Colors::Transparent);
    root_widget_.SetRoot(static_cast<float>(width_), static_cast<float>(height_));
    root_widget_.AddChild(&name_label_);
    root_widget_.AddChild(&nav_anchor_);
    root_widget_.AddChild(speech_bubble_.get());
    name_label_.SetPosition(20.0f, 0.0f, 60.0f, 8.0f);
    name_label_.SetBackgroundColor(media_engine::Colors::Cream70);
    name_label_.SetColor(media_engine::Colors::OrangeDeep);
    name_label_.SetText(name_);
    nav_anchor_.SetPosition(0.0f, 85.0f, 100.0f, 15.0f);

    // ── Per-sprite session ──
    auto& engine = AgentEngine::GetInstance();
    auto& default_roles = engine.GetConfig().default_role;
    std::string effective_role = role_id_.empty() ? (default_roles.empty() ? "default" : default_roles[0]) : role_id_;
    effective_role_id_ = effective_role;
    session_id_ = engine.CreateSession(effective_role, "");
    LOG_INFO("[Sprite] Session '{}' created for '{}' (role='{}')", session_id_, name_, effective_role);

    // Load role TTS config once (doesn't change at runtime)
    if (auto tts = engine.GetRoleTtsConfig(effective_role)) {
        if (!tts->backend.empty()) role_tts_backend_ = tts->backend;
        if (!tts->voice.empty()) role_tts_voice_ = tts->voice;
    }

    // ── Pet loading (textures tied to this sprite window's renderer) ──
    auto binding = LoadSpriteBindingFromRole(effective_role);
    if (!binding.spritesheet_file.empty()) {
        auto& eng_cfg = AgentEngine::GetInstance().GetConfig();
        std::string slug = std::filesystem::path(binding.spritesheet_file).stem().string();
        pet_sprite_ = std::make_unique<Spritesheet>(*sprite_window_, slug,
            eng_cfg.sprite_assets_dir);
        if (pet_sprite_->IsValid()) {
            std::string display = pet_sprite_->GetDisplayName();
            if (!display.empty()) { name_ = display; sprite_window_->SetTitle(display.c_str()); }
            name_label_.SetText(name_);
            LOG_INFO("Loaded pet '{}' from {}", slug, binding.spritesheet_file);
        } else {
            pet_sprite_.reset();
        }
    }
    if (!pet_sprite_ && !binding.assets_dir.empty()) {
        LoadPetFromDir(binding.assets_dir);
    }
    if (!pet_sprite_ && !binding.sprite_id.empty()) {
        LoadPetBySpriteId(binding.sprite_id);
    }

    // Fallback: if no pet loading source provided a display name,
    // use display_name from the role JSON directly.
    if (!binding.display_name.empty() && name_ == effective_role) {
        name_ = binding.display_name;
        sprite_window_->SetTitle(name_.c_str());
    }
    name_label_.SetText(name_);

    // ── Set bubble title and assistant label to sprite display name ──
    speech_bubble_->SetTitle(name_);
    speech_bubble_->SetAssistantRoleName(name_);

    // ── SpeechBubble onSubmit callback ──
    speech_bubble_->SetOnSubmit([this](const std::string& msg) {
        AgentEngine::GetInstance().SendUserMessage(session_id_, msg);
    });

    // ── Global VoiceEngine singleton (ASR + TTS) ──
    VoiceEngine::GetInstance();

    // ── Mouse handler: drag + double-click (+ nav popup) ──
    mc.RegMouseHandler(sprite_window_, [this](const media_engine::MouseEvent& me) {
        switch (me.type) {
            case media_engine::MouseEventType::DOWN:
                DispatchClickAction(me);
                break;
            case media_engine::MouseEventType::MOTION:
                if (dragging_) {
                    ConstrainSpriteOnScreen(me);
                } else {
                    UpdateHoverState(me);
                }
                break;
            case media_engine::MouseEventType::UP:
                EndDrag();
                break;
            case media_engine::MouseEventType::LEAVE:
                SetHovering(false);
                break;
        }
    });

    // ── Render handler: pet + speech bubble (via widget tree) + context menu ──
    mc.RegRenderHandler(sprite_window_, [this]() {
        // Sprite window always uses small font regardless of global font scale
        media_engine::MediaCore::SetWindowFontScale(sprite_window_, ProsophorConfig::kFontScaleSmall);

        // Pre-set snapshot and window size, then render through widget tree cascade
        auto snap = AgentEngine::GetInstance().GetSessionSnapshot(session_id_);
        speech_bubble_->SetSnapshot(snap ? *snap : RenderSnapshot{});
        speech_bubble_->SetOverrideSize(sprite_window_->GetWidth(), sprite_window_->GetHeight());

        root_widget_.Render(media_engine::RenderContext{});

        // ── Debug borders (window outline + sprite hitbox) ──
// #ifndef NDEBUG
//         {
//             int ww = sprite_window_->GetWidth();
//             int wh = sprite_window_->GetHeight();
//             media_engine::DrawList::OverlayRectOutline(0.0f, 0.0f,
//                 static_cast<float>(ww), static_cast<float>(wh),
//                 0.0f, media_engine::Colors::White, 1.5f);
//         }

//         if (pet_sprite_ && pet_sprite_->IsValid()) {
//             auto& b = sprite_bounds_;
//             media_engine::DrawList::OverlayRectOutline(b.x, b.y,
//                 b.width, b.height,
//                 0.0f, media_engine::Colors::White, 1.0f);
//         }
// #endif  // !NDEBUG

        // Global context menu (singleton)
        UIRenderer::Instance().RenderContextMenu(sprite_window_);
    });

    LOG_INFO("[Sprite] Window '{}' created ({}x{})", name_, width_, height_);
    return true;
}

// ── PetCanvas: root widget drawing ───────────────────────────────────────

void Sprite::PetCanvas::Render(const media_engine::RenderContext& ctx) {
    if (!visible_) return;

    auto& sprite = owner_;
    float win_w = width_;
    float win_h = height_;

    // 1. Pet sprite (centered at ground ratio)
    if (sprite.pet_sprite_ && sprite.pet_sprite_->IsValid()) {
        auto pet_cfg = LayoutConfig{};
        float base_x = win_w / 2.0f;
        float base_y = win_h * pet_cfg.pet_ground_ratio;
        auto action = sprite.GetEffectiveAction();
        int frame_count = sprite.pet_sprite_->GetFrameCount(action);
        int fps = sprite.pet_sprite_->GetActionFps(action);
        if (fps <= 0) fps = 10;
        if (frame_count <= 0) frame_count = 1;
        int frame = static_cast<int>(sprite.animation_time_ * fps) % frame_count;

        float sx = base_x - pet_cfg.pet_sprite_size / 2.0f;
        float sy = base_y - pet_cfg.pet_sprite_size / 2.0f;

        sprite.pet_sprite_->RenderFrame(action, frame, sx, sy,
                                    pet_cfg.pet_sprite_size, pet_cfg.pet_sprite_size);
        sprite.sprite_bounds_ = {sx, sy, pet_cfg.pet_sprite_size, pet_cfg.pet_sprite_size};
    }

    // 4. Cascade to widget-tree children (speech_bubble_)
    for (auto* child : children_) {
        child->Render(ctx);
    }
}

// ── Mouse event handlers ─────────────────────────────────────────────

void Sprite::DispatchClickAction(const media_engine::MouseEvent& me) {
    // Right-click on pet → context menu; non-pet areas pass through via native WS_EX_TRANSPARENT
    if (me.button == media_engine::MouseButton::RIGHT) {
        bool on_pet = sprite_bounds_.Contains(static_cast<float>(me.x), static_cast<float>(me.y)) &&
                      static_cast<float>(me.y) < static_cast<float>(height_) - 20.0f;
        if (on_pet) {
            UIRenderer::Instance().RequestContextMenu(me.window);
        }
        return;
    }
    // Only left-click from here
    if (me.button != media_engine::MouseButton::LEFT) {
        return;
    }

    float fx = static_cast<float>(me.x);
    float fy = static_cast<float>(me.y);

    // 1. Pet sprite click → drag / double-click toggle bubble
    //    But skip if speech bubble is visible and hit (don't drag through bubble)
    bool bubble_hit = speech_bubble_ && speech_bubble_->IsVisible() &&
                      speech_bubble_->HitTest(static_cast<int>(fx), static_cast<int>(fy));
    if (sprite_bounds_.Contains(fx, fy) && fy < static_cast<float>(height_) - 20.0f && !bubble_hit) {
        // Double-click → toggle speech bubble
        if (first_click_at_ && SteadyClock::ElapsedMillis(*first_click_at_) < 500) {
            first_click_at_.reset();
            speech_bubble_->Toggle();
            SpriteManager::GetInstance().SetFocusedSession(session_id_);
            return;
        }
        // Single-click — record for potential double-click
        first_click_at_ = SteadyClock::Now();

        // Start drag
        dragging_ = true;
        hover_override_active_ = false;
        auto& mc = media_engine::MediaCore::Instance();
        int wx, wy;
        float gx, gy;
        sprite_window_->GetPosition(&wx, &wy);
        mc.GetGlobalMousePosition(&gx, &gy);
        drag_off_x_ = wx - static_cast<int>(gx);
        drag_off_y_ = wy - static_cast<int>(gy);
        return;
    }

    // 2. Everything else — non-pet clicks natively pass through via WS_EX_TRANSPARENT.
    // Bubble interactive areas (buttons, input) also work via SDL3 raw input.
    // No explicit passthrough or SendInput needed.
}

void Sprite::ConstrainSpriteOnScreen(const media_engine::MouseEvent& /*me*/) {
    auto& mc = media_engine::MediaCore::Instance();
    int pre_drag_x;
    sprite_window_->GetPosition(&pre_drag_x, nullptr);
    float gx, gy;
    mc.GetGlobalMousePosition(&gx, &gy);
    int target_x = static_cast<int>(gx) + drag_off_x_;
    int target_y = static_cast<int>(gy) + drag_off_y_;

    int disp_x, disp_y, disp_w, disp_h;
    if (mc.GetDisplayBoundsForWindow(sprite_window_, &disp_x, &disp_y, &disp_w, &disp_h)) {
        int win_w = sprite_window_->GetWidth();
        int win_h = sprite_window_->GetHeight();
        float sprite_size = LayoutConfig{}.pet_sprite_size;
        int half = static_cast<int>(sprite_size / 2);
        int char_screen_x = target_x + static_cast<int>(win_w / 2.0f - sprite_size / 2.0f);
        int char_screen_y = target_y + static_cast<int>(win_h * LayoutConfig{}.pet_ground_ratio - sprite_size / 2.0f);
        int valid_char_x = std::clamp(char_screen_x, disp_x - half, disp_x + disp_w - half);
        int valid_char_y = std::clamp(char_screen_y, disp_y - half, disp_y + disp_h - half);
        target_x += (valid_char_x - char_screen_x);
        target_y += (valid_char_y - char_screen_y);
    }
    sprite_window_->SetPosition(target_x, target_y);

    if (target_x != pre_drag_x) {
        SetDragOverride(true, target_x < pre_drag_x);
        last_motion_time_ = SteadyClock::Now();
    }
}

void Sprite::UpdateHoverState(const media_engine::MouseEvent& me) {
    float fx = static_cast<float>(me.x);
    float fy = static_cast<float>(me.y);
    SetHovering(sprite_bounds_.Contains(fx, fy));
}

void Sprite::EndDrag() {
    dragging_ = false;
    SetDragOverride(false, false);
    SetHovering(false);
}

// ── Background ────────────────────────────────────────────────────────

// ── Pet binding ─────────────────────────────────────────────────────

Sprite::SpriteBinding Sprite::LoadSpriteBindingFromRole(const std::string& role_id) {
    auto path = (ProsophorConfig::BaseDir() / "roles" / (role_id + ".json")).string();
    if (!FileExists(path)) return {};
    try {
        std::ifstream ifs(path);
        nlohmann::json j;
        ifs >> j;
        std::string sid = j.value("sprite_id", "");
        std::string sp_file = j.value("spritesheet", "");
        std::string assets_dir = j.value("sprite_assets_dir", "");
        std::string display_name = j.value("role_name", "");
        return {sid, assets_dir, sp_file, display_name};
    } catch (const std::exception& e) {
        LOG_WARN("[Sprite] Failed to read role '{}': {}", role_id, e.what());
        return {};
    }
}

void Sprite::LoadPetBySpriteId(const std::string& sprite_id) {
    // Priority 1: config sprite_assets_dir/{sprite_id}/ (subdirectory with meta.json)
    auto& config = AgentEngine::GetInstance().GetConfig();
    if (!config.sprite_assets_dir.empty()) {
        std::string dir = prosophor::JoinPath(config.sprite_assets_dir, sprite_id);
        if (DirExists(dir)) {
            LoadPetFromDir(dir);
            if (pet_sprite_) return;
        }
    }

    // Priority 1b: config sprite_assets_dir/{sprite_id}.webp directly
    if (!config.sprite_assets_dir.empty()) {
        std::string webp_path = prosophor::JoinPath(config.sprite_assets_dir, sprite_id + ".webp");
        if (FileExists(webp_path)) {
            pet_sprite_ = std::make_unique<Spritesheet>(*sprite_window_, sprite_id, config.sprite_assets_dir);
            if (pet_sprite_->IsValid()) {
                std::string display = pet_sprite_->GetDisplayName();
                if (!display.empty()) {
                    name_ = display;
                    sprite_window_->SetTitle(display.c_str());
                }
                name_label_.SetText(name_);
                LOG_INFO("Loaded pet '{}' from {}", sprite_id, webp_path);
                return;
            }
            pet_sprite_.reset();
        }
    }

    LOG_WARN("No sprite found for sprite_id='{}'", sprite_id);
}

void Sprite::LoadPetFromDir(const std::string& assets_dir) {
    if (!DirExists(assets_dir)) {
        LOG_WARN("Sprite assets dir not found: {}", assets_dir);
        return;
    }

    // Extract display name and spritesheet path from meta.json
    std::string display_name;
    std::string spritesheet_file;
    std::string meta_path = assets_dir + "/meta.json";
    if (FileExists(meta_path)) {
        try {
            std::ifstream mf(meta_path);
            nlohmann::json mj;
            mf >> mj;
            display_name = mj.value("display_name", "");
            spritesheet_file = mj.value("spritesheet_path", "");
        } catch (...) {}
    }
    if (display_name.empty()) {
        display_name = std::filesystem::path(assets_dir).filename().string();
    }

    // 同名匹配：从 meta.json 指定的 spritesheet_path 加载
    if (!spritesheet_file.empty()) {
        std::filesystem::path sfp(spritesheet_file);
        std::string slug = sfp.stem().string();
        pet_sprite_ = std::make_unique<Spritesheet>(*sprite_window_, slug, assets_dir + "/");
        if (pet_sprite_->IsValid()) {
            name_ = display_name;
            sprite_window_->SetTitle(display_name.c_str());
            name_label_.SetText(name_);
            LOG_INFO("Loaded sprite '{}' from {}/{}", display_name, assets_dir, spritesheet_file);
            return;
        }
        pet_sprite_.reset();
        LOG_WARN("Spritesheet '{}' not valid in {}, falling back to scan", spritesheet_file, assets_dir);
    }

    // Fallback: scan for the first .webp
    auto load_webp = [&](const std::string& dir) -> bool {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".webp") continue;
            pet_sprite_ = std::make_unique<Spritesheet>(*sprite_window_,
                entry.path().stem().string(), dir + "/");
            if (pet_sprite_->IsValid()) {
                name_ = display_name;
                sprite_window_->SetTitle(display_name.c_str());
                name_label_.SetText(name_);
                LOG_INFO("Loaded sprite '{}' from {}", display_name, dir);
                return true;
            }
            pet_sprite_.reset();
        }
        return false;
    };

    if (load_webp(assets_dir)) return;
    std::string sub_dir = assets_dir + "/sprites";
    if (DirExists(sub_dir) && load_webp(sub_dir)) return;

    LOG_WARN("No valid spritesheet found in assets dir: {}", assets_dir);
}

const std::string& Sprite::GetCurrentPetSlug() const {
    static const std::string s_empty;
    if (pet_sprite_) return pet_sprite_->GetSlug();
    return s_empty;
}

const std::string& Sprite::GetCurrentPetName() const {
    return name_;
}

std::string Sprite::GetSpritesheetPath() const {
    if (pet_sprite_) return pet_sprite_->GetFilePath();
    return "";
}

// ── State → Action ────────────────────────────────────────────────────

SpritesheetAction Sprite::GetEffectiveAction() const {
    // Priority: drag > hover > agent_state
    if (dragging_ || drag_override_active_) return drag_override_left_ ? SpritesheetAction::RUN_LEFT : SpritesheetAction::RUN_RIGHT;
    if (hover_override_active_) return SpritesheetAction::WAVE;

    switch (agent_state_) {
        case AgentRuntimeState::IDLE:
        case AgentRuntimeState::COMPLETE:
        case AgentRuntimeState::STREAM_MODE_COMPLETE:
            return SpritesheetAction::IDLE;
        case AgentRuntimeState::BEGINNING:
        case AgentRuntimeState::STREAM_THINKING_START:
        case AgentRuntimeState::STREAM_THINKING:
        case AgentRuntimeState::STREAM_THINKING_END:
            return SpritesheetAction::REVIEW;
        case AgentRuntimeState::STREAM_TOOL_START:
        case AgentRuntimeState::STREAM_TOOL:
        case AgentRuntimeState::STREAM_TOOL_END:
        case AgentRuntimeState::EXECUTING_TOOL:
        case AgentRuntimeState::TOOL_USE:
            return SpritesheetAction::SPRINT;
        case AgentRuntimeState::WAITING_PERMISSION:
            return SpritesheetAction::WAIT;
        case AgentRuntimeState::STATE_ERROR:
            return SpritesheetAction::FAILED;
        case AgentRuntimeState::STREAM_CONTENT_TYPING:
        case AgentRuntimeState::STREAM_CONTENT_START:
        case AgentRuntimeState::STREAM_CONTENT_END:
            return SpritesheetAction::REVIEW;
    }
    return SpritesheetAction::IDLE;
}

void Sprite::UpdateAnimation(float delta_time) {
    animation_time_ += delta_time;

    // ── Auto-wander when IDLE-like, not dragging, not hovering ──
    if ((agent_state_ == AgentRuntimeState::IDLE ||
         agent_state_ == AgentRuntimeState::COMPLETE ||
         agent_state_ == AgentRuntimeState::STREAM_MODE_COMPLETE) &&
        !dragging_ && !hover_override_active_) {
        if (wandering_) {
            int wx, wy;
            sprite_window_->GetPosition(&wx, &wy);
            float speed = 80.0f;
            int dx = static_cast<int>(wander_left_ ? -speed * delta_time : speed * delta_time);
            int new_wander_x = wx + dx;
            {
                auto& mcref = media_engine::MediaCore::Instance();
                int disp_x, disp_w;
                if (mcref.GetDisplayBoundsForWindow(sprite_window_, &disp_x, nullptr, &disp_w, nullptr)) {
                    int win_w = sprite_window_->GetWidth();
                    float sprite_size = LayoutConfig{}.pet_sprite_size;
                    int half = static_cast<int>(sprite_size / 2);
                    int char_screen_x = new_wander_x + static_cast<int>(win_w / 2.0f - sprite_size / 2.0f);
                    int valid_char_x = std::clamp(char_screen_x, disp_x - half, disp_x + disp_w - half);
                    new_wander_x += (valid_char_x - char_screen_x);
                }
            }
            sprite_window_->SetPosition(new_wander_x, wy);
            wander_dist_ += std::abs(static_cast<float>(dx));
            if (wander_dist_ >= wander_max_dist_) {
                wandering_ = false;
                SetDragOverride(false, false);
                wander_timer_ = SteadyClock::Now();
            }
        } else {
            if (SteadyClock::ElapsedMillis(wander_timer_) > 3000) {
                wander_left_ = std::rand() % 2 == 0;
                wander_dist_ = 0.0f;
                wander_max_dist_ = 80.0f + static_cast<float>(std::rand() % 200);
                wandering_ = true;
                SetDragOverride(true, wander_left_);
            }
        }
    } else {
        if (wandering_) {
            wandering_ = false;
            SetDragOverride(false, false);
        }
        wander_timer_ = SteadyClock::Now();
    }
}

// ── State / override setters ──────────────────────────────────────────

void Sprite::SetAgentState(AgentRuntimeState state, const std::string& details) {
    agent_state_ = state;
    state_details_ = details;
}

void Sprite::HandleAgentOutput(AgentRuntimeState state, const std::string& role_id,
                                const std::string& details, const std::string& delta) {
    SetAgentState(state, details);

    if (state == AgentRuntimeState::STREAM_CONTENT_START) {
        // 每轮重新读角色 TTS：设置面板热切换或 /role 后立即生效
        if (auto tts = AgentEngine::GetInstance().GetRoleTtsConfig(role_id)) {
            if (!tts->backend.empty()) role_tts_backend_ = tts->backend;
            if (!tts->voice.empty()) role_tts_voice_ = tts->voice;
        }
        tts_chunker_.Reset();   // 新一轮正文：丢弃上一轮残留
    }
    if (role_tts_voice_ == "none") {
        tts_chunker_.Reset();
        return;
    }
    if (state == AgentRuntimeState::STREAM_CONTENT_TYPING) {
        tts_chunker_.Push(delta);
    }

    const bool done = (state == AgentRuntimeState::COMPLETE ||
                       state == AgentRuntimeState::STREAM_MODE_COMPLETE);
    const std::string ready = tts_chunker_.TakeReady(done);
    if (!ready.empty()) {
        VoiceEngine::GetInstance().Speak(ready, role_tts_backend_, role_tts_voice_);
    }
}


void Sprite::SetDragOverride(bool active, bool left) {
    drag_override_active_ = active;
    drag_override_left_ = left;
}

void Sprite::SetHovering(bool active) {
    hover_override_active_ = active;
}

void Sprite::ToggleSpeechBubble() {
    speech_bubble_->Toggle();
}

bool Sprite::IsSpeechBubbleVisible() const {
    return speech_bubble_->IsVisible();
}

}  // namespace prosophor
