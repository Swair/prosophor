// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/sprite.h"
#include "virtual_sprite/sprite_manager.h"
#include "virtual_sprite/layout_config.h"
#include "prosophor_core/public/agent_engine.h"
#include "virtual_sprite/components/chat_panel.h"
#include "media_engine/ui_component/input_panel.h"
#include "media_engine/media_engine.h"
#include "common/i18n.h"

#include <unordered_map>

namespace prosophor {

void ChatWindow::RenderChatView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    auto Lc = LayoutConfig{};

    // 右侧角色面板 (展开或折叠手柄)
    if (right_panel_open_) {
        int rp_w = static_cast<int>(static_cast<float>(cont_x + cont_w) * Lc.right_panel_ratio);
        RenderRightPanel(cont_x + cont_w - rp_w, rp_w, cont_y, cont_h);
        cont_w -= rp_w;
    } else {
        float hx = static_cast<float>(cont_x + cont_w) - 10.0f;
        float hy = static_cast<float>(cont_y + cont_h / 2) - 16.0f;
        media_engine::DrawList::RoundRect(hx, hy, 10.0f, 32.0f, Lc.rounding_small,
            media_engine::Colors::CreamBorder);
        if (media_engine::ImGuiWidget::IconButton("right_panel_restore", "\xe2\x97\x80",  // ◀
                hx, hy, 10.0f,
                media_engine::Colors::CreamBorder,
                media_engine::Colors::Gray55, 3.0f)) {
            right_panel_open_ = true;
        }
    }

    // PanelContainer 统一容器 (白色圆角卡 + 标题 + FrameBorder)
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("view_chat").c_str());

    // 消息 + 输入区域 (在 f.a 内定位)
    float inner_x = f.a.x + 4.0f;
    float inner_y = f.a.y;
    float inner_w = f.a.w - 4.0f;
    float inner_h = f.a.h - 20.0f;
    float input_h = Lc.input_area_height;
    float msg_h = inner_h - input_h - 4.0f;

    d_->chat_panel->SetPixelRect(inner_x, inner_y, inner_w, msg_h);
    d_->input_panel->SetPixelRect(inner_x, inner_y + msg_h + 4.0f, inner_w - 6.0f, input_h);

    RenderChatContent();
    RenderTokenSpeed(static_cast<int>(inner_x), static_cast<int>(inner_y),
                     static_cast<int>(inner_w), static_cast<int>(inner_h));
}

void ChatWindow::UpdateLayout(int main_w, int main_h) {
    if (main_w == prev_layout_w_ && main_h == prev_layout_h_) return;
    prev_layout_w_ = main_w;
    prev_layout_h_ = main_h;

    auto Lc = LayoutConfig{};
    float input_pct_h = Lc.input_area_height / static_cast<float>(main_h) * 100.0f;
    float content_pct_h = 100.0f - input_pct_h;

    d_->chat_panel->SetRoot(main_w, main_h);
    d_->chat_panel->SetPosition(0, 0, 100, content_pct_h);
    d_->input_panel->SetRoot(main_w, main_h);
    d_->input_panel->SetPosition(0, 100 - input_pct_h, 100, input_pct_h);
}

void ChatWindow::RenderChatContent() {
    auto& engine = AgentEngine::GetInstance();
    std::string sid = SpriteManager::GetInstance().GetFocusedSession();
    auto snap = sid.empty() ? engine.GetFocusedSessionSnapshot()
                            : engine.GetSessionSnapshot(sid);
    d_->chat_panel->SetSnapshot(snap.value_or(RenderSnapshot{}));

    std::string sprite_name = SpriteManager::GetInstance().GetFocusedSpriteName();
    if (!sprite_name.empty()) d_->chat_panel->SetAssistantRoleName(sprite_name);

    d_->chat_panel->Render(media_engine::RenderContext{});
    d_->input_panel->Render(media_engine::RenderContext{});
}

void ChatWindow::RenderRightPanel(int panel_x, int panel_w, int panel_y, int panel_h) {
    auto& L = I18n::Instance();
    float sm = Spacing();
    float x = static_cast<float>(panel_x);
    float y = static_cast<float>(panel_y);
    float h = static_cast<float>(panel_h);

    float tog_x = x - 14.0f;
    float tog_y = y + h / 2.0f - 20.0f;
    if (media_engine::ImGuiWidget::IconButton("right_panel_toggle", "\xe2\x96\xb6",  // ▶
            tog_x, tog_y, 14.0f,
            media_engine::Colors::CreamBorder,
            media_engine::Colors::Gray55, 2.0f)) {
        right_panel_open_ = false;
    }

    media_engine::DrawList::RoundRect(x, y, static_cast<float>(panel_w), h, 0,
        media_engine::Colors::CreamLight);
    media_engine::DrawList::RoundRect(x, y + 3.0f, 2.0f, h - 3.0f, 0,
        media_engine::Colors::OrangeWarm);
    media_engine::DrawList::RoundRect(x + 1, y + 3.0f, 1.0f, h - 3.0f, 0,
        media_engine::Colors::CreamBorder);

    float cx = x + static_cast<float>(panel_w) - 14.0f;
    media_engine::DrawList::CircleFilled(cx, y + 12.0f, 5.0f, media_engine::Colors::OrangeLight);
    media_engine::DrawList::CircleFilled(cx, y + 12.0f, 3.0f, media_engine::Colors::OrangeWarm);

    media_engine::Layout::SetCursorScreenPos(x + 8.0f, y + 8.0f);
    auto _panel = media_engine::ScopedChild(
        "role_panel", static_cast<float>(panel_w) - 7.0f, h - 30.0f);

    {
        media_engine::DrawList::RoundRect(x + 8.0f, 22.0f, 3.0f, 12.0f, 1.5f,
            media_engine::Colors::Orange);
        media_engine::Layout::Dummy(8, 0);
        media_engine::Layout::SameLine();
        media_engine::Text::Colored(media_engine::Colors::OrangeWarm,
            L.Get("role_panel_header").c_str());
    }
    media_engine::DrawList::RoundRect(x + 12.0f, 40.0f,
        static_cast<float>(panel_w) - 24.0f, 1.0f, 0, media_engine::Colors::CreamBorder);
    media_engine::Layout::Dummy(0, 4.0f * sm);

    auto& sprites = SpriteManager::GetInstance().GetAll();
    std::string focused_sid = SpriteManager::GetInstance().GetFocusedSession();
    float card_w = static_cast<float>(panel_w) - 20.0f;

    auto _spacing = media_engine::ScopedStyleVar::ItemSpacing(4.0f, 2.0f);

    for (auto& s : sprites) {
        bool focused = (s->GetSessionId() == focused_sid);
        float cx2, cy;
        media_engine::Layout::GetCursorScreenPos(&cx2, &cy);

        if (media_engine::ImGuiWidget::InvisibleButton(
                ("card_" + s->GetSessionId()).c_str(), card_w, 64.0f))
            SpriteManager::GetInstance().SetFocusedSession(s->GetSessionId());

        bool hov = media_engine::ImGuiWidget::IsItemHovered();
        FocusCard(cx2, cy, card_w, 64.0f, focused, hov);

        float thumb_x = cx2 + 8.0f;
        float thumb_y = cy + 8.0f;
        std::string tex_path = s->GetSpritesheetPath();
        if (!tex_path.empty()) {
            auto it = d_->thumbnails.find(tex_path);
            if (it == d_->thumbnails.end()) {
                auto tex = std::make_unique<media_engine::Texture>(
                    *d_->window, tex_path);
                if (tex->GetOriginWidth() > 0 && tex->GetOriginHeight() > 0)
                    it = d_->thumbnails.emplace(tex_path, std::move(tex)).first;
            }
            if (it != d_->thumbnails.end() && it->second)
                it->second->DrawImGui(thumb_x, thumb_y, 48.0f, 48.0f,
                    0.0f, 0.0f, 1.0f / 8.0f, 1.0f / 9.0f);
        }

        media_engine::DrawList::Text(cx2 + 64.0f, cy + 24.0f,
            focused ? media_engine::Colors::OrangeDeep
                    : hov ? media_engine::Colors::Orange
                    : media_engine::Colors::Black,
            s->GetName().c_str());

        media_engine::Layout::Dummy(0, 0);
    }
}

void ChatWindow::RenderTokenSpeed(int main_x, int main_y, int main_w, int main_h) {
    auto snap = SpriteManager::GetInstance().GetFocusedSession().empty()
        ? AgentEngine::GetInstance().GetFocusedSessionSnapshot()
        : AgentEngine::GetInstance().GetSessionSnapshot(
            SpriteManager::GetInstance().GetFocusedSession());
    if (!snap || snap->streaming_token_speed <= 0.0f) return;

    auto Lc = LayoutConfig{};
    int speed_val = static_cast<int>(snap->streaming_token_speed + 0.5f);
    std::string text = std::to_string(speed_val) + " tok/s";

    float pad = Lc.token_badge_pad;
    float text_w = static_cast<float>(text.size()) * Lc.token_badge_char_w;
    float text_h = Lc.token_badge_h;
    float badge_w = text_w + pad * 2;
    float badge_h = text_h + pad * 2;

    float wx, wy;
    media_engine::ImGuiWindow::GetPos(&wx, &wy);
    float bx = wx + static_cast<float>(main_x) + static_cast<float>(main_w) - badge_w - 8.0f;
    float by = wy + static_cast<float>(main_y) + static_cast<float>(main_h) - badge_h - 64.0f;

    media_engine::DrawList::RoundRect(bx, by, badge_w, badge_h, Lc.rounding_small,
        media_engine::Colors::WhiteTranslucent);
    media_engine::DrawList::Text(bx + pad, by + pad,
        media_engine::Colors::Gray55, text.c_str());
}

} // namespace prosophor
