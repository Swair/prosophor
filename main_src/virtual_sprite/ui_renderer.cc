// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/ui_renderer.h"
#include "common/i18n.h"
#include "media_engine/media_engine.h"
#include "virtual_sprite/sprite.h"
#include "virtual_sprite/sprite_manager.h"
#include "prosophor_core/public/agent_engine.h"

namespace prosophor {

UIRenderer& UIRenderer::Instance() {
    static UIRenderer instance;
    return instance;
}

UIRenderer::UIRenderer() = default;
UIRenderer::~UIRenderer() = default;

void UIRenderer::RequestContextMenu(media_engine::Window* win) {
    if (win) {
        context_menu_requests_.push_back(win);
    }
}

void UIRenderer::RenderContextMenu(media_engine::Window* current_win) {
    if (!visible_ || !current_win) return;

    // Only open popup if this window requested it
    auto it = std::find(context_menu_requests_.begin(), context_menu_requests_.end(), current_win);
    if (it != context_menu_requests_.end()) {
        media_engine::Popup::Open("sprite_context_menu");
        context_menu_requests_.erase(it);
    }

    auto _col = media_engine::ScopedColors(media_engine::Color::Slot::PopupBg,
                                         media_engine::Colors::CreamOpaque90)
                .Then(media_engine::Color::Slot::Text, media_engine::Colors::Black);
    auto _popup = media_engine::ScopedPopupMenu("sprite_context_menu");
    if (!_popup) return;

    auto& L = I18n::Instance();
    if (media_engine::Popup::MenuItem(L.Get("ctx_chat").c_str())) {
        if (on_toggle_chat_) on_toggle_chat_(current_win);
    }
    if (media_engine::Popup::MenuItem(L.Get("ctx_show_main").c_str())) {
        if (on_show_main_window_) on_show_main_window_();
    }
    // Tools toggle per-sprite
    {
        auto* sprite = SpriteManager::GetInstance().FindByWindow(current_win);
        if (sprite) {
            auto& engine = AgentEngine::GetInstance();
            const std::string& session_id = sprite->GetSessionId();
            bool tools_on = engine.GetSessionUseTools(session_id);
            const char* tools_label = tools_on
                ? L.Get("ctx_disable_tools").c_str()
                : L.Get("ctx_enable_tools").c_str();
            if (media_engine::Popup::MenuItem(tools_label)) {
                engine.SetSessionUseTools(session_id, !tools_on);
            }
        }
    }
    media_engine::ImGuiWidget::Separator();
    if (media_engine::Popup::MenuItem(L.Get("ctx_quit").c_str())) {
        media_engine::MediaCore::Instance().Quit();
    }
}

}  // namespace prosophor
