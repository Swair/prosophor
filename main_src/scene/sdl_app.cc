// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/sdl_app.h"
#include "scene/office_background.h"
#include "scene/agent_state_observer.h"
#include "scene/office_character_manager.h"
#include "scene/pixel_character.h"
#include "media_core.h"
#include "scene/ui_renderer.h"
#include "common/log_wrapper.h"
#include "common/config.h"

#include <memory>
#include "cli/agent_commander.h"

namespace aicode {

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

    // Set AgentCommander to SDL mode
    auto& commander = AgentCommander::GetInstance();
    commander.SetMode(RunMode::SDL);

    aicode::AgentCommander::GetInstance();

    MediaCore::Instance().MediaInit(2500, 1400);
    MediaCore::Instance().SetFPS(60);

    AgentStateVisualizer::GetInstance().Initialize();
    UIRenderer::Instance().Initialize();
    OfficeBackground::GetInstance().Initialize();
    PixelCharacterRenderer::Instance().Initialize();

    UIRenderer::Instance().SetOnMessageSubmit([this](const std::string& message) {
        if (!message.empty() && message[0] == '/') {
            if (aicode::AgentCommander::GetInstance().HandleCommand(message)) {
                UIRenderer::Instance().AddMessage("system", message);
            } else {
                UIRenderer::Instance().AddMessage("system", "命令执行失败：" + message);
            }
            return;
        }

        aicode::AgentCommander::GetInstance().ProcessUserMessage(message);
    });

    // Register event handler for MediaCore events
    MediaCore::Instance().RegEventHandler([this](std::vector<EventType>& event_list) {
        for (const auto& event : event_list) {
            switch (event) {
                case EventType::ESCAPE:
                    // Interrupt event
                    HandleKeyDown(0);  // Send as key event
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
        OfficeCharacterManager::Instance().Update(dt);
    });

    MediaCore::Instance().RegRenderHandler([]() {
        OfficeBackground::GetInstance().Render();
        AgentStateVisualizer::GetInstance().Render();
        UIRenderer::Instance().Render();
        UIRenderer::Instance().RenderImGui();
    });

    UIRenderer::Instance().SetStatePropsGetter([](aicode::AgentRuntimeState state) {
        switch (state) {
            case aicode::AgentRuntimeState::IDLE:
                return aicode::StateVisualProps{100, 100, 100, 255, "Idle"};
            case aicode::AgentRuntimeState::THINKING:
                return aicode::StateVisualProps{65, 105, 225, 255, "Thinking"};
            case aicode::AgentRuntimeState::EXECUTING_TOOL:
                return aicode::StateVisualProps{255, 165, 0, 255, "Executing"};
            case aicode::AgentRuntimeState::WAITING_PERMISSION:
                return aicode::StateVisualProps{255, 255, 0, 255, "Waiting"};
            case aicode::AgentRuntimeState::STATE_ERROR:
                return aicode::StateVisualProps{255, 0, 0, 255, "Error"};
            case aicode::AgentRuntimeState::COMPLETE:
                return aicode::StateVisualProps{0, 255, 0, 255, "Complete"};
            default:
                return aicode::StateVisualProps{128, 128, 128, 255, "Unknown"};
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

}  // namespace aicode
