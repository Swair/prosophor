// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/sprite.h"
#include "virtual_sprite/sprite_manager.h"
#include "virtual_sprite/layout_config.h"

#include "agent_engine.h"
#include "config/config.h"
#include "virtual_sprite/components/chat_panel.h"
#include "media_engine/ui_component/input_panel.h"
#include "media_engine/media_engine.h"
#include "common/log_wrapper.h"
#include "common/i18n.h"
#include "virtual_sprite/voice_engine.h"
#include "managers/active_trigger_manager.h"
#include "virtual_sprite/panels/panel_helpers.h"

#include <cstring>
#include <unordered_map>
#include <future>
#include <thread>
#include <ctime>

namespace prosophor {

ChatWindow::ChatWindow() = default;
ChatWindow::~ChatWindow() = default;

bool ChatWindow::Create(int width, int height) {
    width_ = width;
    height_ = height;

    media_engine::WindowConfig cfg;
    cfg.resizable = true;
    auto* win = media_engine::MediaCore::Instance().CreateMediaWindow(
        (I18n::Instance().Get("window_title") + " v" PROSOPHOR_VERSION).c_str(), width_, height_, cfg);

    int disp_w = 0, disp_h = 0;
    media_engine::MediaCore::GetPrimaryDisplaySize(&disp_w, &disp_h);
    win->SetMinSize(
        std::max(800, static_cast<int>(disp_w * 0.5f)),
        std::max(600, static_cast<int>(disp_h * 0.5f)));

    auto chat_panel = std::make_unique<ChatPanel>(0, 0, 100, 100);
    auto input_panel = std::make_unique<media_engine::InputPanel>(0, 0, 100, 100);
    input_panel->SetSendButtonColor(media_engine::Colors::Orange);
    if (submit_cb_) input_panel->SetOnSubmit(submit_cb_);

    d_ = std::make_unique<PanelData>();
    d_->window = win;
    d_->chat_panel = std::move(chat_panel);
    d_->input_panel = std::move(input_panel);

    VoiceEngine::GetInstance();
    d_->input_panel->SetOnMicToggle([this](bool on) {
        if (!d_) return;
        if (on) VoiceEngine::GetInstance().StartCapture();
        else VoiceEngine::GetInstance().StopCapture();
    });

    sidebar_.SetRenderWindow(win);

    media_engine::MediaCore::Instance().RegRenderHandler(win, [this]() {
        auto _app = media_engine::ScopedColors(
            media_engine::Color::Slot::WindowBg, media_engine::Colors::MilkyWhite)
            .Then(media_engine::Color::Slot::Text, media_engine::Colors::Gray40)
            .Then(media_engine::Color::Slot::FrameBg, media_engine::Colors::White)
            .Then(media_engine::Color::Slot::PopupBg, media_engine::Colors::White)
            .Then(media_engine::Color::Slot::Border, media_engine::Colors::Gray63);
        Render();
        if (d_) {
            std::string text = VoiceEngine::GetInstance().PollAsrLatest();
            if (!text.empty()) d_->input_panel->SetText(text);
        }
    });

    CreateTrayWindow();
    LOG_INFO("[ChatWindow] Created {}x{} window", width_, height_);
    return true;
}

void ChatWindow::SetOnSubmit(SubmitCallback cb) {
    submit_cb_ = cb;
    if (d_ && d_->input_panel) d_->input_panel->SetOnSubmit(cb);
}

void ChatWindow::SetVisible(bool visible) {
    visible_ = visible;
    if (d_ && d_->window) {
        if (visible) { d_->window->Show(); ShowTray(false); }
        else { d_->window->Hide(); ShowTray(true); }
    }
}

media_engine::Window* ChatWindow::GetWindow() const {
    return d_ ? d_->window : nullptr;
}

void ChatWindow::Render() {
    if (!visible_ || !d_ || !d_->window) return;
    update_handler_.CheckAndShowUpdate();
    update_handler_.Render();
    RenderChatUI();
}

// ============================================================================
// Shell Layout
// ============================================================================
void ChatWindow::RenderChatUI() {
    int win_w = d_->window->GetWidth();
    int win_h = d_->window->GetHeight();

    media_engine::ImGuiWindow::SetNextPos(0, 0);
    media_engine::ImGuiWindow::SetNextSize(static_cast<float>(win_w), static_cast<float>(win_h));
    media_engine::ImGuiWindow::SetNextBgAlpha(0.0f);
    bool root_open = true;
    auto _root = media_engine::ScopedGuard(
        media_engine::ImGuiWindow::Begin("chat_root", &root_open,
            media_engine::ImGuiWindowFlags_NoDecoration |
            media_engine::ImGuiWindowFlags_NoMove |
            media_engine::ImGuiWindowFlags_NoSavedSettings |
            media_engine::ImGuiWindowFlags_NoScrollWithMouse),
        []{ media_engine::ImGuiWindow::End(); }, true);
    if (!_root) return;

    media_engine::DrawList::RoundRect(0, 0, static_cast<float>(win_w),
        static_cast<float>(win_h), 0, media_engine::Colors::MilkyWhite);

    // 1. Sidebar (left)
    sidebar_.Render(win_w, win_h);
    int sb_w = sidebar_.GetWidth();

    // 2. Content area (right of sidebar)
    int cont_x = sb_w;
    int cont_y = 0;
    int cont_w = win_w - sb_w;
    int cont_h = win_h;

    RenderCurrentView(cont_x, cont_y, cont_w, cont_h);
}



// ============================================================================
// View Router
// ============================================================================
void ChatWindow::RenderCurrentView(int cont_x, int cont_y, int cont_w, int cont_h) {
    switch (sidebar_.GetActiveItem()) {
        case NavItem::Chat:         RenderChatView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Status:       RenderStatusView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Sessions:     RenderSessionsView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Logs:         RenderLogsView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Usage:        RenderUsageView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Config:       RenderConfigView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Roles:        RenderRolesView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Memory:       RenderMemoryView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Providers:    RenderProvidersView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::ActiveTriggers: RenderActiveTriggersView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::About:        RenderAboutView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::LocalModels:  RenderLocalModelsView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Tts:          RenderTtsView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Security:     RenderSecurityView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Skills:       RenderSkillsView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::KnowledgeBase: RenderKnowledgeView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::Mcp:          RenderMcpView(cont_x, cont_y, cont_w, cont_h); break;
        case NavItem::PetStore:         RenderPetStoreView(cont_x, cont_y, cont_w, cont_h); break;
        default:                        RenderChatView(cont_x, cont_y, cont_w, cont_h); break;
    }
}

// ============================================================================
// Shared helpers for all panel views
// ============================================================================
// ============================================================================
// Active Triggers View
// ============================================================================
void ChatWindow::RenderActiveTriggersView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("nav_active_triggers").c_str());
    float sm = Spacing();

    auto& triggers = ActiveTriggerManager::GetInstance();
    auto plugins = triggers.ListPlugins();

    float iy = f.a.y + 8.0f * sm;
    if (plugins.empty()) {
        media_engine::DrawList::Text(f.a.x + 4.0f, iy, media_engine::Colors::Gray55,
            L.Get("panel_no_data").c_str());
        return;
    }
    for (auto& p : plugins) {
        media_engine::DrawList::Text(f.a.x + 4.0f, iy, media_engine::Colors::Gray40,
            p.name.c_str());
        iy += 22.0f * sm;
        std::string info = p.mode + "  interval=" + std::to_string(p.interval)
                         + "s  priority=" + std::to_string(p.priority);
        media_engine::DrawList::Text(f.a.x + 16.0f, iy, media_engine::Colors::Gray55,
            info.c_str());
        iy += 24.0f * sm;
    }
}

// ============================================================================
// About Window
// ============================================================================
void ChatWindow::RenderAboutWindow() {
    if (!about_open_) return;
    auto& L = I18n::Instance();
    media_engine::Popup::Open("about_modal");
    media_engine::ImGuiWindow::SetNextSize(400.0f, 300.0f, media_engine::ImGuiCond_Appearing);
    const auto about_title = L.Get("about_title") + "###about_modal";
    auto _about = media_engine::ScopedModal(
        about_title.c_str(), &about_open_, media_engine::ImGuiWindowFlags_NoSavedSettings);
    if (!_about) return;

    float sm = Spacing();
    std::string title = L.Get("app_name") + " v" PROSOPHOR_VERSION;
    media_engine::Text::Colored(media_engine::Colors::OrangeDeep, title.c_str());
    media_engine::Layout::Dummy(0, 14.0f * sm);
    media_engine::Text::Wrapped("AI Desktop Companion \xe2\x80\x94 Desktop Pet + LLM Chat",
        media_engine::ImGuiWindow::GetWidth() - 30.0f, media_engine::Colors::Gray40);
    media_engine::Layout::Dummy(0, 20.0f * sm);
    media_engine::Text::Colored(media_engine::Colors::Gray47, L.Get("about_contact").c_str());
    media_engine::Layout::Dummy(0, 10.0f * sm);
    media_engine::Text::Fmt("Email: %s", "swair_fang@126.com");
    media_engine::Layout::Dummy(0, 10.0f * sm);
    media_engine::Text::Fmt("GitHub: %s", "https://github.com/Swair");
    media_engine::Layout::Dummy(0, 24.0f * sm);

    float btn_w = 80.0f;
    media_engine::Layout::SetCursorPosX(media_engine::ImGuiWindow::GetWidth() - btn_w - 10.0f);
    if (media_engine::ImGuiWidget::Button(L.Get("btn_close").c_str(), btn_w, 0))
        about_open_ = false;
}

// ============================================================================
// Tray Window
// ============================================================================
void ChatWindow::CreateTrayWindow() {
    media_engine::WindowConfig tcfg;
    tcfg.borderless = true;
    tcfg.transparent_window = true;
    tcfg.transparent_bg = true;
    tcfg.skip_taskbar = true;
    tcfg.always_on_top = true;
    tcfg.resizable = false;
    tcfg.use_shared_font = false;

    LayoutConfig tcfg_layout;
    tray_window_ = media_engine::MediaCore::Instance().CreateMediaWindow(
        "prosophor_tray", tcfg_layout.tray_icon_size, tcfg_layout.tray_icon_size, tcfg);
    if (!tray_window_) { LOG_WARN("[ChatWindow] Failed to create tray window"); return; }

    int dw, dh;
    media_engine::MediaCore::GetPrimaryDisplaySize(&dw, &dh);
    tray_window_->SetPosition(dw - tcfg_layout.tray_margin, dh - tcfg_layout.tray_margin);
    tray_window_->Hide();

    std::string icon_path = (ProsophorConfig::BaseDir() / "resources" / "preview.png").string();
    tray_texture_ = std::make_unique<media_engine::Texture>(*tray_window_, icon_path);

    media_engine::MediaCore::Instance().RegRenderHandler(tray_window_, [this]() { RenderTray(); });
    media_engine::MediaCore::Instance().RegMouseHandler(tray_window_,
        [this](const media_engine::MouseEvent& me) {
            if (me.type == media_engine::MouseEventType::DOWN &&
                me.button == media_engine::MouseButton::RIGHT)
                media_engine::MediaCore::Instance().Quit();
            else if (me.type == media_engine::MouseEventType::DOWN &&
                     me.button == media_engine::MouseButton::LEFT) {
                ShowTray(false);
                SetVisible(true);
            }
        });
}

void ChatWindow::ShowTray(bool show) {
    tray_showing_ = show;
    if (tray_window_) { if (show) tray_window_->Show(); else tray_window_->Hide(); }
}

void ChatWindow::RenderTray() {
    float s = static_cast<float>(LayoutConfig{}.tray_icon_size);
    media_engine::ImGuiWindow::SetNextPos(0, 0);
    media_engine::ImGuiWindow::SetNextSize(s, s);
    bool canvas_open = true;
    int canvas_flags = 0
        | media_engine::ImGuiWindowFlags_NoDecoration
        | media_engine::ImGuiWindowFlags_NoMove
        | media_engine::ImGuiWindowFlags_NoMouseInputs
        | media_engine::ImGuiWindowFlags_NoBackground
        | media_engine::ImGuiWindowFlags_NoSavedSettings;
    if (!media_engine::ImGuiWindow::Begin("tray", &canvas_open, canvas_flags)) {
        media_engine::ImGuiWindow::End();
        return;
    }
    float pad = 1.0f;
    if (tray_texture_)
        tray_texture_->DrawImGui(pad, pad, s - pad * 2.0f, s - pad * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    media_engine::ImGuiWindow::End();
}


}  // namespace prosophor
