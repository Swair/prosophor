// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/components/sidebar.h"
#include "virtual_sprite/layout_config.h"
#include "prosophor_core/public/config.h"
#include "common/i18n.h"
#include "media_engine/media_engine.h"
#include "platform/platform.h"

namespace prosophor {

Sidebar::Sidebar() {
    group_expanded_[SidebarGroup::Capabilities] = true;
    group_expanded_[SidebarGroup::Monitor] = true;
    group_expanded_[SidebarGroup::Agents] = true;
    group_expanded_[SidebarGroup::Store] = true;
    group_expanded_[SidebarGroup::Settings] = true;
}

Sidebar::~Sidebar() = default;

bool Sidebar::IsGroupExpanded(SidebarGroup group) const {
    auto it = group_expanded_.find(group);
    return it != group_expanded_.end() && it->second;
}

void Sidebar::ToggleGroup(SidebarGroup group) {
    group_expanded_[group] = !IsGroupExpanded(group);
}

int Sidebar::GetWidth() const {
    return sidebar_width_;
}

    void Sidebar::Render(int win_w, int win_h) {
        auto& L18n = I18n::Instance();
        auto L = LayoutConfig{};
        sidebar_width_ = collapsed_ ? L.sidebar_width_collapsed
                                    : std::clamp(static_cast<int>(static_cast<float>(win_w) * L.sidebar_width_ratio), 180, 280);
        int sb_w = sidebar_width_;
        float fw = static_cast<float>(sb_w);
        float fh = static_cast<float>(win_h);
        float s = ProsophorConfig::GetInstance().font_scale;

        // 背景 + 右边框
        media_engine::DrawList::RoundRect(0, 0, fw, fh, 0, media_engine::Colors::MilkyWhite);
        media_engine::DrawList::RoundRect(fw - 1, 0, 1.0f, fh, 0, media_engine::Colors::CreamBorder);

        RenderBrand();

        // 导航项在可滚动子窗口中，footer 在根窗口中
        int nav_top = static_cast<int>(static_cast<float>(L.sidebar_brand_height));
        int nav_h = static_cast<int>(static_cast<float>(win_h) - static_cast<float>(nav_top)
                      - static_cast<float>(L.sidebar_footer_height) * s);
        // computed outside child scope for RenderFooter alignment
        float label_x = L.sb_icon_x * s + (L.sb_icon_w + L.sb_icon_text_gap) * s;
        {
            media_engine::ImGuiWindow::SetNextPos(0.0f, static_cast<float>(nav_top));
            auto _thin_scroll = media_engine::ScopedStyleVar::ScrollbarSize(2.0f);
            auto _child = media_engine::ScopedChild(
                "sidebar_nav", static_cast<float>(sb_w), static_cast<float>(nav_h),
                0, 0);

            // 全部使用子窗口相对坐标 (y 从 0 开始)
            float scroll_y = media_engine::Scroll::GetY();
            float y = 0;
            float item_h = static_cast<float>(L.sidebar_nav_item_h) * s;
            float icon_x = L.sb_icon_x * s;
            float base_y = static_cast<float>(nav_top) - scroll_y;
            float indent_s = L.sb_child_indent * s;

            auto nav_item = [&](NavItem item, const char* fallback_icon, const char* label, float indent = 0) {
                const char* icon = fallback_icon;
                float item_icon_x = collapsed_ ? icon_x : icon_x + indent;
                float item_label_x = label_x + indent;
                float sy = base_y + y;
                float left = indent > 0 ? icon_x : 0;
                float text_oy = (item_h - L.sb_text_h) / 2.0f;

                media_engine::Layout::SetCursorPos(left, y);
                if (media_engine::ImGuiWidget::InvisibleButton(
                        ("nb" + std::to_string(static_cast<int>(item))).c_str(), fw - left, item_h)) {
                    active_item_ = item;
                    if (on_nav_) on_nav_(item);
                }
                bool hov = media_engine::ImGuiWidget::IsItemHovered();
                bool act = (active_item_ == item);

                constexpr float kPad = 4.0f;
                float sx = kPad;
                float sw = fw - kPad * 2;
                if (act) {
                    media_engine::DrawList::RoundRect(sx, sy, sw, item_h, 4.0f,
                        media_engine::Colors::OrangeLightest);
                    media_engine::DrawList::RoundRect(sx, sy, L.sb_act_bar_w, item_h, 4.0f,
                        media_engine::Colors::Orange);
                } else if (hov) {
                    media_engine::DrawList::RoundRect(sx, sy, sw, item_h, 4.0f,
                        media_engine::Colors::OrangePale);
                }

                auto icon_col = act ? media_engine::Colors::OrangeDeep
                             : hov ? media_engine::Colors::Orange
                             : media_engine::Colors::Gray55;
                auto txt_col = act ? media_engine::Colors::OrangeDeep
                             : hov ? media_engine::Colors::Orange
                             : media_engine::Colors::Gray40;

                media_engine::DrawList::Text(item_icon_x, sy + text_oy, icon_col, icon);
                if (!collapsed_) {
                    media_engine::DrawList::Text(item_label_x, sy + text_oy, txt_col, label);
                }
                y += item_h;
            };

            auto collapse_group = [&](SidebarGroup group, const char* text) {
                if (!collapsed_) {
                    float gh = static_cast<float>(L.sidebar_group_label_h) * s;
                    float text_oy = (gh - L.sb_text_h) / 2.0f;
                    float gy = base_y + y + text_oy;
                    float toggle_x = icon_x;
                    float label_x2 = toggle_x + 20.0f * s;  // between nav icon gap (32*s) and text
                    bool expanded = IsGroupExpanded(group);
                    const char* toggle = expanded ? "\xe2\x96\xbe" : "\xe2\x96\xb8"; // ▾ / ▸

                    media_engine::Layout::SetCursorPos(toggle_x, y);
                    if (media_engine::ImGuiWidget::InvisibleButton(
                            ("grp" + std::to_string(static_cast<int>(group))).c_str(), fw - toggle_x, gh)) {
                        ToggleGroup(group);
                    }
                    bool hov = media_engine::ImGuiWidget::IsItemHovered();
                    if (hov)
                        media_engine::DrawList::RoundRect(0, base_y + y, fw, gh, 4.0f,
                            media_engine::Colors::OrangeLightest);
                    media_engine::DrawList::Text(toggle_x, gy,
                        hov ? media_engine::Colors::OrangeDeep : media_engine::Colors::Orange, toggle);
                    media_engine::DrawList::Text(label_x2, gy,
                        hov ? media_engine::Colors::OrangeDeep : media_engine::Colors::Gray55, text);
                }
                y += L.sb_group_child_gap * s;
            };

        // ── TOP BAR ── (always visible)
        nav_item(NavItem::Chat,   "\xf0\x9f\x92\xac", L18n.Get("nav_chat").c_str());
        nav_item(NavItem::ActiveTriggers, "\xf0\x9f\x94\x94", L18n.Get("nav_active_triggers").c_str());
        nav_item(NavItem::Status, "\xf0\x9f\x93\x8a", L18n.Get("nav_status").c_str());
        nav_item(NavItem::Sessions, "\xf0\x9f\x93\x8b", L18n.Get("nav_sessions").c_str());

        // ═══ 智能体 ═══
        collapse_group(SidebarGroup::Agents, L18n.Get("sidebar_group_agents").c_str());
        if (IsGroupExpanded(SidebarGroup::Agents)) {
            nav_item(NavItem::PetStore, "\xf0\x9f\x90\xb1", L18n.Get("nav_petstore").c_str(), indent_s);
            nav_item(NavItem::KnowledgeBase, "\xf0\x9f\x93\x9a", L18n.Get("nav_knowledge").c_str(), indent_s);
        }

        // ═══ 服务 ═══
        collapse_group(SidebarGroup::Capabilities, L18n.Get("sidebar_group_capabilities").c_str());
        if (IsGroupExpanded(SidebarGroup::Capabilities)) {
            nav_item(NavItem::Roles,      "\xf0\x9f\x91\xa4", L18n.Get("nav_roles").c_str(), indent_s);
            nav_item(NavItem::Memory,     "\xf0\x9f\xa7\xa0", L18n.Get("nav_memory").c_str(), indent_s);
            nav_item(NavItem::Skills,     "\xf0\x9f\x9b\xa0", L18n.Get("nav_skills").c_str(), indent_s);
            nav_item(NavItem::Mcp,        "\xf0\x9f\x94\x8c", L18n.Get("nav_mcp").c_str(), indent_s);
        }

        // ═══ 监控 ═══
        collapse_group(SidebarGroup::Monitor, L18n.Get("sidebar_group_monitor").c_str());
        if (IsGroupExpanded(SidebarGroup::Monitor)) {
            nav_item(NavItem::Usage,     "\xf0\x9f\x93\x88", L18n.Get("nav_usage").c_str(), indent_s);
            nav_item(NavItem::Security,  "\xf0\x9f\x9b\xa1", L18n.Get("nav_security").c_str(), indent_s);
            nav_item(NavItem::Logs,      "\xf0\x9f\x93\x9d", L18n.Get("nav_logs").c_str(), indent_s);
        }

        // ═══ 设置 ═══
        collapse_group(SidebarGroup::Settings, L18n.Get("sidebar_group_settings").c_str());
        if (IsGroupExpanded(SidebarGroup::Settings)) {
            nav_item(NavItem::Config,     "\xe2\x9a\x99", L18n.Get("nav_config").c_str(), indent_s);
            nav_item(NavItem::Providers,  "\xf0\x9f\x97\x84", L18n.Get("nav_providers").c_str(), indent_s);
            nav_item(NavItem::LocalModels,"\xf0\x9f\x93\xa6", L18n.Get("nav_local_models").c_str(), indent_s);
            nav_item(NavItem::Tts,        "\xf0\x9f\x97\xa3", L18n.Get("nav_tts").c_str(), indent_s);
        }

        // About
        nav_item(NavItem::About, "\xe2\x9d\x93", L18n.Get("nav_about").c_str());
    }
    // 子窗口在此关闭
    RenderFooter(win_h, label_x);
}

void Sidebar::RenderBrand() {
    int sb_w = GetWidth();
    float fw = static_cast<float>(sb_w);
    float bh = static_cast<float>(LayoutConfig{}.sidebar_brand_height);
    float s = ProsophorConfig::GetInstance().font_scale;

    // 延迟加载 logo
    if (!logo_tex_ && render_window_) {
        std::string icon_path = (ProsophorConfig::BaseDir() / "resources" / "preview.png").string();
        logo_tex_ = std::make_unique<media_engine::Texture>(*render_window_, icon_path);
    }

    // 背景
    media_engine::DrawList::RoundRect(0, 0, fw, bh, 0, media_engine::Colors::White);

    if (!collapsed_) {
        // Logo
        float logo_size = 56.0f * s;
        float logo_x = 10.0f * s;
        float logo_y = (bh - logo_size) / 2.0f;
        if (logo_tex_) {
            logo_tex_->DrawImGui(logo_x, logo_y, logo_size, logo_size, 0, 0, 1, 1);
        }
        // 应用名 (紧挨 Logo 右侧)
        media_engine::DrawList::Text(logo_x + logo_size + 8.0f, logo_y + 12.0f,
            media_engine::Colors::OrangeDeep, "Prosophor");
        // 底部橙色线
        media_engine::DrawList::RoundRect(0, bh - 2.0f, fw, 2.0f, 0,
            media_engine::Colors::Orange);
    } else {
        // 折叠时只显示小图标
        float logo_size = 36.0f * s;
        float logo_x = (fw - logo_size) / 2.0f;
        float logo_y = (bh - logo_size) / 2.0f;
        if (logo_tex_) {
            logo_tex_->DrawImGui(logo_x, logo_y, logo_size, logo_size, 0, 0, 1, 1);
        } else {
            media_engine::DrawList::Text(fw / 2.0f - 7.0f, logo_y + 10.0f,
                media_engine::Colors::OrangeDeep, "P");
        }
    }

    // 点击品牌区域切换折叠
    media_engine::Layout::SetCursorPos(0, 0);
    if (media_engine::ImGuiWidget::InvisibleButton("brand_toggle", fw, bh)) {
        collapsed_ = !collapsed_;
    }
}

void Sidebar::RenderFooter(int win_h, float label_x) {
    auto L = LayoutConfig{};
    int sb_w = GetWidth();
    float fw = static_cast<float>(sb_w);
    float footer_h = static_cast<float>(L.sidebar_footer_height) + 20.0f + 16.0f;
    float fy = static_cast<float>(win_h) - footer_h;
    float lx = L.sb_footer_left_pad;
    float btn_size = 18.0f;

    // 分隔线
    media_engine::DrawList::RoundRect(lx, fy + 42.0f, fw - lx * 2, 1, 0,
        media_engine::Colors::CreamBorder);

    float sep_y = fy + 42.0f;

    if (!collapsed_) {
        auto& i18n = I18n::Instance();
        float link_h = 18.0f;
        float yo = sep_y + 12.0f;

        // ⭐ Star 链接 (GitHub, 可点击)
        float ly = yo;
        media_engine::DrawList::Text(lx, ly, media_engine::Colors::Orange, "\xe2\xad\x90");
        media_engine::DrawList::Text(label_x, ly, media_engine::Colors::Gray55, "GitHub Star");
        media_engine::Layout::SetCursorPos(lx, ly);
        if (media_engine::ImGuiWidget::InvisibleButton("link_github", fw - lx * 2, link_h)) {
            Platform::OpenWithDefault("https://github.com/Swair");
        }

        // 🌐 官方网站
        ly = yo + 22.0f;
        media_engine::DrawList::Text(lx, ly, media_engine::Colors::Gray55, "\xf0\x9f\x8c\x90");
        media_engine::DrawList::Text(label_x, ly, media_engine::Colors::Gray55, "aicodingbox.com");
        media_engine::Layout::SetCursorPos(lx, ly);
        if (media_engine::ImGuiWidget::InvisibleButton("link_web", fw - lx * 2, link_h)) {
            Platform::OpenWithDefault("http://aicodingbox.com");
        }

        // 📦 版本号 + 语言切换 + 折叠按钮 (底部)
        ly = yo + 46.0f;
        std::string ver = "\xf0\x9f\x93\xa6 v" PROSOPHOR_VERSION;
        media_engine::DrawList::Text(lx, ly, media_engine::Colors::Gray55, ver.c_str());
        float btn_y = yo + 44.0f;
        bool is_zh = i18n.GetLanguage() == "zh-CN";
        if (media_engine::ImGuiWidget::IconButton("lang_footer",
                is_zh ? "En" : "Ch",
                fw - btn_size * 2 - 10.0f, btn_y, btn_size,
                media_engine::Colors::CreamBorder,
                media_engine::Colors::Gray55, 3.0f)) {
            i18n.SetLanguage(is_zh ? "en" : "zh-CN");
        }

    } else {
        // 折叠时只保留切换按钮
        float btn_y = fy + (footer_h - btn_size) / 2.0f;
        if (media_engine::ImGuiWidget::IconButton("sidebar_toggle",
                "\xe2\x96\xb6", fw - btn_size - 6.0f, btn_y, btn_size,  // ▶
                media_engine::Colors::CreamBorder,
                media_engine::Colors::Gray55, 3.0f)) {
            collapsed_ = !collapsed_;
        }
    }
}

} // namespace prosophor
