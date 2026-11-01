#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/components/panel_kit.h"
#include "virtual_sprite/components/item_list.h"
#include "virtual_sprite/layout_config.h"
#include "common/i18n.h"
#include "prosophor_core/public/skill.h"
#include "prosophor_core/public/agent_engine.h"
#include "prosophor_core/public/core_types.h"
#include "prosophor_core/public/config.h"
#include <filesystem>
#include <fstream>

namespace prosophor {

static std::vector<SkillMetadata> s_skills;
static int s_sel = -1;
static bool s_scan = true;
static std::string s_edit_content;
static std::string s_edit_id;

// Chat state for skills_creator
static std::string s_chat_session_id;
static bool s_chat_open = false;

static void ScanSkills() {
    s_skills.clear();
    auto skills_dir = std::filesystem::path(ProsophorConfig::DefaultConfigPath()).parent_path() / "skills";
    auto& loader = SkillLoader::GetInstance();
    s_skills = loader.LoadSkillsFromDirectory(skills_dir);
    if (s_sel >= (int)s_skills.size()) s_sel = 0;
    s_scan = false;
}

void ChatWindow::RenderSkillsView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("view_skills").c_str(), 30.0f);
    auto Lc = LayoutConfig{};
    float sm = Spacing();

    if (s_scan) ScanSkills();

    float gap = 12.0f;
    float left_w = Lc.panel_left_list_w;
    auto sv = SplitPanel(f.a, left_w, f.btn_h, gap);

    // Left: skill list
    {
        media_engine::Layout::SetCursorScreenPos(sv.left_x, sv.left_y);
        auto _l = media_engine::ScopedChild("sk_list", sv.left_w, sv.inner_h, 0,
            media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        ItemList list(sv.left_x, sv.left_y, sv.left_w);
        for (int i = 0; i < (int)s_skills.size(); ++i) {
            auto& sk = s_skills[i];
            std::string name = sk.name.empty() ? sk.id : sk.name;
            if (!sk.emoji.empty()) name = sk.emoji + " " + name;
            if (list.Item(("sk_" + sk.id).c_str(), name.c_str(), i == s_sel))
                s_sel = i;
        }
    }

    sv.DrawDivider();

    // Right: selected skill detail
    {
        media_engine::Layout::SetCursorScreenPos(sv.right_x, sv.right_y);
        auto _r = media_engine::ScopedChild("sk_cfg", sv.right_w, sv.inner_h, 0,
            media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (s_sel < 0 || s_sel >= (int)s_skills.size()) {
            media_engine::Text::Colored(media_engine::Colors::Gray55,
                s_skills.empty() ? L.Get("panel_no_data").c_str() : "Select a skill");
        } else if (!s_chat_open) {
            auto& sk = s_skills[s_sel];
            float x = sv.right_x;
            float w = sv.right_w - Lc.section_card_right_margin;
            float y = sv.right_y + 8.0f;

            // Info card
            float sx = x + Lc.section_card_pad;
            float sw = w - Lc.section_card_w_extra;
            float info_h = 120.0f;
            media_engine::DrawList::RoundRect(sx, y, sw, info_h, Lc.panel_radius, media_engine::Colors::Beige);
            media_engine::DrawList::RoundRectOutline(sx, y, sw, info_h, Lc.panel_radius, media_engine::Colors::Gray63, 1.0f);

            std::string title = sk.name.empty() ? sk.id : sk.name;
            if (!sk.emoji.empty()) title = sk.emoji + " " + title;
            media_engine::DrawList::Text(x + Lc.label_row_pad, y + 10.0f, media_engine::Colors::OrangeDeep, title.c_str());

            y += 40.0f;
            if (!sk.description.empty()) {
                media_engine::DrawList::Text(x + 22.0f, y, media_engine::Colors::Gray55, sk.description.c_str());
                y += 22.0f;
            }
            media_engine::DrawList::Text(x + 22.0f, y, media_engine::Colors::Gray55,
                ("Commands: " + std::to_string(sk.commands.size())).c_str());
            y += 22.0f;

            if (!sk.installs.empty()) {
                media_engine::Layout::SetCursorScreenPos(x + 22.0f, y);
                if (media_engine::ImGuiWidget::Button("Install", 100.0f, 0)) {
                    auto& loader = SkillLoader::GetInstance();
                    loader.InstallSkill(sk);
                }
                media_engine::Layout::Dummy(0, 4.0f * sm);
            }

            // Content editor
            auto skill_md = sk.root_dir / "SKILL.md";
            if (std::filesystem::exists(skill_md)) {
                if (s_edit_id != sk.id) {
                    std::ifstream ifs(skill_md);
                    s_edit_content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
                    s_edit_id = sk.id;
                }

                float cy = sv.right_y + info_h + 12.0f;
                float ch = std::max(60.0f, sv.inner_h - info_h - 90.0f);
                media_engine::DrawList::RoundRect(sx, cy, sw, ch, Lc.panel_radius, media_engine::Colors::White);
                media_engine::DrawList::RoundRectOutline(sx, cy, sw, ch, Lc.panel_radius, media_engine::Colors::CreamBorder, 1.0f);
                media_engine::DrawList::Text(x + Lc.label_row_pad, cy + 10.0f, media_engine::Colors::OrangeDeep, "Content");

                cy += 36.0f;
                s_edit_content.resize(8192);
                float edit_w = sw - 40.0f;
                float edit_h = ch - 46.0f;
                media_engine::Layout::SetCursorScreenPos(x + 22.0f, cy);
                auto _w = media_engine::ScopedItemWidth(edit_w);
                media_engine::ImGuiWidget::InputTextMultiline("##sk_edit", s_edit_content.data(), s_edit_content.size(),
                    edit_w, edit_h, false);
                s_edit_content.resize(std::strlen(s_edit_content.data()));
            }
        }

        // ─── AI Creator (bottom drawer, expands upward) ───
        float sx = sv.right_x + Lc.section_card_pad;
        float sw = sv.right_w - Lc.section_card_w_extra - Lc.section_card_pad;
        float bar_y = sv.right_y + sv.inner_h - f.btn_h - 32.0f;

        // Bottom bar (always visible) — click to toggle up
        std::string arrow = s_chat_open ? " ▼" : " ▲";
        media_engine::DrawList::RoundRect(sx, bar_y, sw, 28.0f, Lc.rounding_small, media_engine::Colors::OrangeLightest);
        media_engine::DrawList::Text(sv.right_x + Lc.label_row_pad, bar_y + 6.0f,
            media_engine::Colors::OrangeDeep, ("AI Skill Creator" + arrow).c_str());
        media_engine::Layout::SetCursorScreenPos(sx, bar_y);
        if (media_engine::ImGuiWidget::InvisibleButton("##sk_chat_toggle", sw, 28.0f))
            s_chat_open = !s_chat_open;

        // Expanded: overlay upward from the bar (covers content area above)
        if (s_chat_open) {
            float overlay_top = sv.right_y + 4.0f;
            float overlay_h = bar_y - overlay_top - 4.0f;
            if (overlay_h > 80.0f) {
                // Overlay background
                media_engine::DrawList::RoundRect(sx, overlay_top, sw, overlay_h, Lc.panel_radius,
                    media_engine::Colors::White);
                media_engine::DrawList::RoundRectOutline(sx, overlay_top, sw, overlay_h, Lc.panel_radius,
                    media_engine::Colors::CreamBorder, 1.0f);

                float msg_h = overlay_h - 50.0f;
                float input_y = overlay_top + msg_h + 4.0f;
                media_engine::Layout::SetCursorScreenPos(sx + 4.0f, overlay_top + 4.0f);
                {
                    auto _scroll = media_engine::ScopedChild("sk_chat_msgs", sw - 8.0f, msg_h, 0,
                        media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

                    auto& engine = AgentEngine::GetInstance();
                    auto snap = s_chat_session_id.empty()
                        ? std::nullopt
                        : engine.GetSessionSnapshot(s_chat_session_id);

                    if (snap && !snap->messages.empty()) {
                        for (auto& msg : snap->messages) {
                            std::string rl = (msg.role == "user") ? "You:" : "AI:";
                            media_engine::Text::Colored(
                                msg.role == "user" ? media_engine::Colors::Gray20 : media_engine::Colors::OrangeDeep,
                                rl.c_str());
                            media_engine::Text::Wrapped(msg.text().c_str(), sw - 16.0f,
                                media_engine::Colors::Gray20);
                            media_engine::Layout::Dummy(0, 4.0f * sm);
                        }
                        if (!snap->streaming_text.empty()) {
                            media_engine::Text::Colored(media_engine::Colors::OrangeDeep, "AI:");
                            media_engine::Text::Wrapped(snap->streaming_text.c_str(), sw - 16.0f,
                                media_engine::Colors::Gray20);
                        }
                    }
                }

                // Input (Enter submits)
                static media_engine::InputText s_ai_chat_in("##sk_chat_in", "", 512);
                static bool s_ai_chat_in_init = false;
                if (!s_ai_chat_in_init) {
                    s_ai_chat_in.SetEnterReturnsTrue(true);
                    s_ai_chat_in_init = true;
                }
                media_engine::Layout::SetCursorScreenPos(sx + 4.0f, input_y);
                auto _iw = media_engine::ScopedItemWidth(sw - 68.0f);
                s_ai_chat_in.Render();
                bool do_send = s_ai_chat_in.IsEnterPressed();
                media_engine::Layout::SameLine();
                if (media_engine::ImGuiWidget::Button(L.Get("btn_send").c_str(), 60.0f, 0))
                    do_send = true;
                if (do_send) {
                    std::string msg = s_ai_chat_in.GetText();
                    if (!msg.empty()) {
                        auto& engine = AgentEngine::GetInstance();
                        if (s_chat_session_id.empty())
                            s_chat_session_id = engine.CreateSession("skills_creator", "Help me create a new skill.");
                        engine.SendUserMessage(s_chat_session_id, msg);
                        s_ai_chat_in.SetText("");
                    }
                }
            }
        }
    }

    // Save / Refresh / Cancel buttons (hidden when AI Creator expanded)
    if (!s_chat_open)
    {
        float by = f.a.y + f.a.h - f.btn_h - 4.0f;
        float btn_w = Lc.panel_save_btn_w, btn_d = Lc.panel_btn_gap;
        float bx = f.a.x + f.a.w - (btn_w * 3 + btn_d * 2) - Lc.panel_btn_right_gap;

        media_engine::Layout::SetCursorScreenPos(bx, by);
        if (media_engine::ImGuiWidget::Button(L.Get("btn_save").c_str(), btn_w, 0)) {
            if (s_sel >= 0 && s_sel < (int)s_skills.size()) {
                auto md = s_skills[s_sel].root_dir / "SKILL.md";
                if (std::filesystem::exists(md)) {
                    std::ofstream ofs(md);
                    ofs << s_edit_content;
                }
            }
            s_scan = true;
        }
        media_engine::Layout::SameLine();
        media_engine::Layout::Dummy(btn_d, 0);
        media_engine::Layout::SameLine();
        if (media_engine::ImGuiWidget::Button(L.Get("nav_refresh").c_str(), btn_w, 0))
            s_scan = true;
        media_engine::Layout::SameLine();
        media_engine::Layout::Dummy(btn_d, 0);
        media_engine::Layout::SameLine();
        if (media_engine::ImGuiWidget::Button(L.Get("btn_cancel").c_str(), btn_w, 0))
            s_scan = true;
    }
}

} // namespace prosophor
