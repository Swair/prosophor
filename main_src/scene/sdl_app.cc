// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/sdl_app.h"
#include "scene/agent_state_observer.h"
#include "scene/galgame_mode.h"
#include "scene/ui_renderer.h"
#include "scene/layout_config.h"
#include "media_engine/media_engine.h"
#include "common/log_wrapper.h"
#include "common/config.h"

#include <memory>
#include "cli/agent_commander.h"

namespace prosophor {

SdlApp& SdlApp::GetInstance() {
    static SdlApp instance;
    return instance;
}

SdlApp::SdlApp() = default;

SdlApp::~SdlApp() {
    Shutdown();
}

void SdlApp::HandleTextInput(const char* text) {
    if (input_callback_) {
        InputEvent event;
        event.source = InputSource::SDL;
        event.type = InputEvent::Type::Text;
        event.data = TextInputEvent{text, true};
        input_callback_(event);
    }
}

void SdlApp::HandleKeyDown(int key_code) {
    if (current_scene_ == UIMode::GALGAME) {
        GalgameScene::Instance().HandleKeyDown(key_code);
        return;
    }
    if (input_callback_) {
        InputEvent event;
        event.source = InputSource::SDL;
        event.type = InputEvent::Type::Key;
        event.data = KeyEvent{key_code, false, false, false};
        input_callback_(event);
    }
}

void SdlApp::HandleMouseButtonDown(int x, int y) {
    if (input_callback_) {
        InputEvent event;
        event.source = InputSource::SDL;
        event.type = InputEvent::Type::Mouse;
        event.data = MouseEvent{MouseEvent::Click, x, y, 0};
        input_callback_(event);
    }
}

void SdlApp::SetInputCallback(InputCallback callback) {
    input_callback_ = callback;
}

void SdlApp::Initialize() {
    LOG_INFO("Initializing SDL application...");

    MediaCore::Instance().MediaInit(2500, 1400);
    MediaCore::Instance().SetFPS(60);

    AgentStateVisualizer::GetInstance().Initialize();
    GalgameScene::Instance().Initialize();
    UIRenderer::Instance().Initialize();
    HomeScreen::GetInstance().Initialize();

    // Set state props getter (used by UIRenderer when in virtual human mode)
    UIRenderer::Instance().SetStatePropsGetter([](prosophor::AgentRuntimeState state) {
        switch (state) {
            case prosophor::AgentRuntimeState::IDLE:
                return prosophor::StateVisualProps{100, 100, 100, 255, "Idle"};
            case prosophor::AgentRuntimeState::BEGINNING:
                return prosophor::StateVisualProps{65, 105, 225, 255, "Thinking"};
            case prosophor::AgentRuntimeState::EXECUTING_TOOL:
                return prosophor::StateVisualProps{255, 165, 0, 255, "Executing"};
            case prosophor::AgentRuntimeState::WAITING_PERMISSION:
                return prosophor::StateVisualProps{255, 255, 0, 255, "Waiting"};
            case prosophor::AgentRuntimeState::STATE_ERROR:
                return prosophor::StateVisualProps{255, 0, 0, 255, "Error"};
            case prosophor::AgentRuntimeState::COMPLETE:
                return prosophor::StateVisualProps{0, 255, 0, 255, "Complete"};
            default:
                return prosophor::StateVisualProps{128, 128, 128, 255, "Idle"};
        }
    });

    // Home screen mode select callback
    HomeScreen::GetInstance().SetOnModeSelect([this](UIMode mode) {
        SwitchMode(mode);
    });

    // Register event handler for MediaCore events
    MediaCore::Instance().RegEventHandler([this](std::vector<EventType>& event_list) {
        for (const auto& event : event_list) {
            switch (event) {
                case EventType::ESCAPE:
                    // Return to home from virtual human/Terminal mode
                    if (current_scene_ != UIMode::HOME) {
                        SwitchMode(UIMode::HOME);
                    } else {
                        HandleKeyDown(0);
                    }
                    break;
                case EventType::ENTER:
                case EventType::KP_ENTER:
                    HandleKeyDown('\n');
                    break;
                case EventType::BACKSPACE:
                    HandleKeyDown('\b');
                    break;
                default:
                    break;
            }
        }
    });

    MediaCore::Instance().RegUpdateHandler([]() {
        float dt = MediaCore::Instance().GetDeltaTimeS();
        AgentStateVisualizer::GetInstance().Update(dt);
        GalgameScene::Instance().Update(dt);
    });

    MediaCore::Instance().RegRenderHandler([this]() {
        switch (current_scene_) {
            case UIMode::HOME:
                HomeScreen::GetInstance().Render();
                break;
            case UIMode::VIRTUAL_HUMAN:
                RenderVirtualHuman();
                break;
            case UIMode::GALGAME:
                RenderGalgame();
                break;
            case UIMode::TERMINAL:
                break;
        }
    });

    LOG_INFO("SDL application initialized successfully.");
}

void SdlApp::Shutdown() {
    LOG_INFO("Shutting down SDL application...");
    MediaCore::Instance().ImGuiShutdown();
    LOG_INFO("SDL application shutdown complete.");
}

int SdlApp::Run() {
    try {
        Initialize();
        MediaCore::Instance().MainRun();
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("SDL app fatal error: {}", e.what());
        return 1;
    }
}

void SdlApp::Stop() {
    MediaCore::Instance().Quit();
}

void SdlApp::SwitchMode(UIMode mode) {
    if (current_scene_ == mode) return;

    LOG_INFO("Switching mode: {} -> {}", static_cast<int>(current_scene_), static_cast<int>(mode));
    current_scene_ = mode;

    switch (mode) {
        case UIMode::HOME:
            // Restore default input callback behavior for home
            input_callback_ = saved_callback_;
            break;

        case UIMode::VIRTUAL_HUMAN: {
            // Set up virtual human mode
            auto& commander = AgentCommander::GetInstance();
            commander.SetMode(RunMode::SDL);

            // Save previous callback and set new one
            saved_callback_ = input_callback_;
            UIRenderer::Instance().SetOnMessageSubmit([this](const std::string& message) {
                if (!message.empty() && message[0] == '/') {
                    if (prosophor::AgentCommander::GetInstance().HandleCommand(message)) {
                        UIRenderer::Instance().SendToChatPanel("system", message);
                    } else {
                        UIRenderer::Instance().SendToChatPanel("system", "命令执行失败：" + message);
                    }
                    return;
                }
                prosophor::AgentCommander::GetInstance().ProcessUserMessage(message);
            });

            // Show the UI
            UIRenderer::Instance().SetVisible(true);
            AgentStateVisualizer::GetInstance().SetVisible(true);
            break;
        }

        case UIMode::GALGAME: {
            // Set up GALGAME mode
            auto& commander = AgentCommander::GetInstance();
            commander.SetMode(RunMode::SDL);

            saved_callback_ = input_callback_;
            UIRenderer::Instance().SetOnMessageSubmit([this](const std::string& message) {
                if (!message.empty() && message[0] == '/') {
                    if (prosophor::AgentCommander::GetInstance().HandleCommand(message)) {
                        UIRenderer::Instance().SendToChatPanel("system", message);
                    } else {
                        UIRenderer::Instance().SendToChatPanel("system", "命令执行失败：" + message);
                    }
                    return;
                }
                prosophor::AgentCommander::GetInstance().ProcessUserMessage(message);
            });
            break;
        }

        case UIMode::TERMINAL:
            saved_callback_ = input_callback_;
            break;
    }
}

void SdlApp::RenderHome() {
    HomeScreen::GetInstance().Render();
}

void SdlApp::RenderVirtualHuman() {
    AgentStateVisualizer::GetInstance().Render();
    UIRenderer::Instance().Render();
    UIRenderer::Instance().RenderImGui();
}

void SdlApp::RenderGalgame() {
    GalgameScene::Instance().Render();
    UIRenderer::Instance().Render();
    UIRenderer::Instance().RenderImGui();
}

void SdlApp::RenderTerminal() {
    // Terminal mode: just show the UI renderer (ChatPanel + InputPanel + StatusBar)
    UIRenderer::Instance().Render();
    UIRenderer::Instance().RenderImGui();
}

}  // namespace prosophor
