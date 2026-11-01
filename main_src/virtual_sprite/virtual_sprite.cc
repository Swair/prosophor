// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/virtual_sprite.h"
#include "virtual_sprite/sprite.h"
#include "virtual_sprite/ui_renderer.h"
#include "virtual_sprite/layout_config.h"
#include "media_engine/media_engine.h"
#include "common/log_wrapper.h"
#include "common/i18n.h"
#include "prosophor_core/public/agent_engine.h"
#include "prosophor_core/public/config.h"
#include "prosophor_core/public/token_tracker.h"

#include "platform/platform.h"
#include "updater/update_checker.h"
#include <memory>

namespace prosophor {

VirtualSprite& VirtualSprite::GetInstance() {
    static VirtualSprite instance;
    return instance;
}

VirtualSprite::VirtualSprite() = default;

VirtualSprite::~VirtualSprite() {
    if (!shutdown_) Shutdown();
}

void VirtualSprite::HandleTextInput(const char* text) {
    if (input_callback_) {
        InputEvent event;
        event.source = InputSource::SDL;
        event.type = InputEvent::Type::Text;
        event.data = TextInputEvent{text, true};
        input_callback_(event);
    }
}

void VirtualSprite::HandleKeyDown(int key_code) {
    if (input_callback_) {
        InputEvent event;
        event.source = InputSource::SDL;
        event.type = InputEvent::Type::Key;
        event.data = KeyEvent{key_code, false, false, false};
        input_callback_(event);
    }
}

void VirtualSprite::HandleMouseButtonDown(int x, int y) {
    if (input_callback_) {
        InputEvent event;
        event.source = InputSource::SDL;
        event.type = InputEvent::Type::Mouse;
        event.data = MouseEvent{MouseEvent::Click, x, y, 0};
        input_callback_(event);
    }
}

void VirtualSprite::SetInputCallback(InputCallback callback) {
    input_callback_ = callback;
}

void VirtualSprite::GlobalInit() {
    LOG_INFO("Initializing SDL application...");

    // Initialise i18n (load default translations)
    I18n::Instance().Init("zh-CN");

    auto& media = media_engine::MediaCore::Instance();
    media.MediaInit();
    media.SetFPS(60);

    // ── Create central chat window FIRST (becomes primary window) ──
    {
        int disp_w = 1920, disp_h = 1080;
        media_engine::MediaCore::GetPrimaryDisplaySize(&disp_w, &disp_h);
        int win_w = static_cast<int>(disp_w * 0.7f);
        int win_h = static_cast<int>(disp_h * 0.7f);
        central_window_.Create(win_w, win_h);
        central_window_.SetVisible(true);

        // Apply font scale from config (requires ImGui context, which window creates)
        {
            auto& fs = ProsophorConfig::GetInstance().font_scale;
            fs = (fs < ProsophorConfig::kFontScaleSwitch) ? ProsophorConfig::kFontScaleSmall : ProsophorConfig::kFontScaleLarge;
            media_engine::MediaCore::SetGlobalFontScale(fs);
        }

        // Load daily token usage history from per-date files
        auto token_dir = ProsophorConfig::BaseDir() / "token";
        prosophor::EnsureDirectory(token_dir.string());
        TokenTracker::GetInstance().SetDailyDir(token_dir.string());

        central_window_.SetOnSubmit([](const std::string& msg) {
            auto& engine = AgentEngine::GetInstance();
            std::string sid = SpriteManager::GetInstance().GetFocusedSession();
            if (sid.empty()) {
                auto snap = engine.GetFocusedSessionSnapshot();
                sid = snap ? snap->session_id
                           : engine.CreateSession(
                               engine.GetConfig().default_role.empty()
                                   ? "default"
                                   : engine.GetConfig().default_role[0], "");
            }
            engine.SendUserMessage(sid, msg);
        });

        LOG_INFO("Central window created (primary)");
    }

    // ── ASR 状态日志（provider 由 VoiceEngine 首次使用时创建） ──
    {
        const auto& asr_config = ProsophorConfig::GetInstance().asr;
        if (asr_config.enabled) {
            LOG_INFO("ASR enabled (server={})", asr_config.server_url);
        } else {
            LOG_INFO("ASR disabled in config");
        }
    }

    // Route session state changes to the matching sprite
    AgentEngine::GetInstance().SetOutputCallback(
        [](const std::string& session_id, const std::string& role_id,
               AgentRuntimeState state, const std::string& state_msg,
               const std::optional<MessageSchema>& /*reply*/, const std::string& delta) {
            if (auto* s = SpriteManager::GetInstance().FindBySessionId(session_id)) {
                s->HandleAgentOutput(state, role_id, state_msg, delta);
            }
        });

    // Right-click context menu callbacks — toggle the requesting sprite's bubble
    UIRenderer::Instance().SetOnToggleChat([](media_engine::Window* win) {
        if (auto* s = SpriteManager::GetInstance().FindByWindow(win)) {
            s->ToggleSpeechBubble();
        }
    });
    UIRenderer::Instance().SetOnShowMainWindow([this]() {
        central_window_.SetVisible(true);
    });
    UIRenderer::Instance().SetOnNewSprite([]() {
        static int counter = 1;
        auto& vs = VirtualSprite::GetInstance();
        auto* s = SpriteManager::GetInstance().CreateSprite(
            "Assistant " + std::to_string(counter++),
            LayoutConfig{}.sprite_window_width, LayoutConfig{}.sprite_window_height);
        if (s) {
            s->SetOnToggleCentralWindow([&vs]() {
                vs.GetCentralWindow().SetVisible(!vs.GetCentralWindow().IsVisible());
            });
        }
    });

    // Route user input from central window to focused session is handled in
    // central_window_.SetOnSubmit above.

    // Global event handler (keyboard)
    media.RegEventHandler([this](std::vector<media_engine::EventType>& event_list) {
        for (const auto& event : event_list) {
            switch (event) {
                case media_engine::EventType::ENTER:
                case media_engine::EventType::KP_ENTER:
                    HandleKeyDown('\n');
                    break;
                case media_engine::EventType::BACKSPACE:
                    HandleKeyDown('\b');
                    break;
                case media_engine::EventType::ESCAPE:
                    if (central_window_.IsVisible()) {
                        central_window_.SetVisible(false);
                    }
                    break;
                default:
                    break;
            }
        }
    });

    // Helper: create sprite with role_id + wire toggle central window
    auto create_sprite = [this](const std::string& name, int w, int h,
                                 const std::string& role_id = "") -> Sprite* {
        auto* s = SpriteManager::GetInstance().CreateSprite(name, w, h, role_id);
        if (s) {
            s->SetOnToggleCentralWindow([this]() {
                central_window_.SetVisible(!central_window_.IsVisible());
            });
        }
        return s;
    };

    // Create sprites bound to roles (one per default_role entry)
    LayoutConfig sprite_cfg;
    auto& role_list = AgentEngine::GetInstance().GetConfig().default_role;
    for (const auto& role_id : role_list) {
        create_sprite(role_id, sprite_cfg.sprite_window_width, sprite_cfg.sprite_window_height, role_id);
    }

    // Set initial focus to the first sprite's session
    auto& sprites = SpriteManager::GetInstance().GetAll();
    if (!sprites.empty())
        SpriteManager::GetInstance().SetFocusedSession(sprites[0]->GetSessionId());

    // Global update: animate all sprites
    media.RegUpdateHandler([]() {
        float dt = media_engine::MediaCore::Instance().GetDeltaTimeS();
        SpriteManager::GetInstance().UpdateAll(dt);
    });

    // Wire "+New" nav button to create additional sprites at runtime
    SpriteManager::GetInstance().SetOnNewSprite([]() {
        static int counter = 1;
        auto& vs = VirtualSprite::GetInstance();
        auto* s = SpriteManager::GetInstance().CreateSprite(
            "Assistant " + std::to_string(counter++),
            LayoutConfig{}.sprite_window_width, LayoutConfig{}.sprite_window_height);
        if (s) {
            s->SetOnToggleCentralWindow([&vs]() {
                vs.GetCentralWindow().SetVisible(!vs.GetCentralWindow().IsVisible());
            });
        }
    });

    // ── Check for updates (async, SDL GUI only) ─────────────────
    UpdateChecker::Instance().CheckForUpdate();

    LOG_INFO("SDL application initialized successfully.");
}

void VirtualSprite::Shutdown() {
    LOG_INFO("Shutting down SDL application...");
    // Flush today's token usage one last time on shutdown
    TokenTracker::GetInstance().FlushToday();
    SpriteManager::GetInstance().Clear();
    media_engine::MediaCore::Instance().Shutdown();
    LOG_INFO("SDL application shutdown complete.");
    shutdown_ = true;
}

int VirtualSprite::Run() {
    try {
        GlobalInit();
        media_engine::MediaCore::Instance().MainRun();
    } catch (const std::exception& e) {
        LOG_ERROR("SDL app fatal error: {}", e.what());
        Shutdown();
        return 1;
    }
    Shutdown();
    return 0;
}

void VirtualSprite::Stop() {
    media_engine::MediaCore::Instance().Quit();
}

}  // namespace prosophor
