// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/components/panel_kit.h"
#include "virtual_sprite/components/item_list.h"
#include "virtual_sprite/layout_config.h"
#include "common/i18n.h"
#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/config.h"
#include "media_engine/media_engine.h"
#include "prosophor_core/public/agent_engine.h"
#include "prosophor_core/public/memory.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;
namespace prosophor {

namespace {

// ── Data structures ──
struct MemEntry {
    std::string title;
    std::string date;      // date string for sorting
    std::string source;    // "daily" or category name (e.g. "decisions", "exit_summary")
    std::string role;      // role name (empty for daily notes)
    std::string path;
    std::string preview;
    std::string content;   // full content, loaded on demand
};

// ── Static state (persistent across ImGui frames) ──
static std::vector<MemEntry> s_entries;
static int s_sel = -1;
static bool s_scan = true;
static std::string s_detail_content;
static bool s_editing = false;
static std::string s_edit_content;
static std::string s_edit_path;
static std::string s_search_query;
static int s_type_filter = 0;  // 0=All, 1=design_decision, 2=code_change, 3=unresolved_issue, 4=lesson_learned
static int s_role_filter = 0;  // 0=All Roles, 1+=specific role index
static std::vector<std::string> s_role_names;
static std::vector<const char*> s_role_cstrs;
static std::string s_error_msg;
static int s_error_timer = 0;  // frames remaining to show error

// ── Helpers ──
static const char* kTypeLabels[] = {
    "📋 全部",
    "🎨 设计决策",
    "️✏ 代码变更",
    "❓ 未解决问题",
    "📖 经验教训"
};
static const char* kTypeKeys[] = {
    "", "design_decision", "code_change", "unresolved_issue", "lesson_learned"
};

void SetError(const std::string& msg) {
    s_error_msg = msg;
    s_error_timer = 180;  // ~3 seconds at 60fps
}

std::string ReadFileContent(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        SetError("打开失败: " + path);
        return "";
    }
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string FirstLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    if (line.size() > 80) line = line.substr(0, 80) + "...";
    return line;
}

bool MatchesTypeFilter(const std::string& content, int filter_idx) {
    if (filter_idx == 0) return true;  // All
    // Check if content contains the type key
    return content.find(kTypeKeys[filter_idx]) != std::string::npos;
}

bool MatchesSearch(const MemEntry& e, const std::string& query) {
    if (query.empty()) return true;
    std::string q_lower = query;
    std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), ::tolower);

    auto contains = [&](const std::string& s) -> bool {
        std::string s_lower = s;
        std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);
        return s_lower.find(q_lower) != std::string::npos;
    };

    if (contains(e.title) || contains(e.role) || contains(e.source) || contains(e.preview))
        return true;
    // Load content and check
    if (e.content.empty()) {
        // Only load if needed for search
        auto content = ReadFileContent(e.path);
        return contains(content);
    }
    return contains(e.content);
}

void ScanAllMemories() {
    s_entries.clear();
    auto base = ProsophorConfig::BaseDir();

    // 1) Role memories: ~/.prosophor/memories/<role_id>/*/
    auto mem_base = base / "memories";
    if (fs::exists(mem_base)) {
        for (auto& role_dir : fs::directory_iterator(mem_base)) {
            if (!role_dir.is_directory()) continue;
            std::string role_name = role_dir.path().filename().string();
            for (auto& cat_dir : fs::directory_iterator(role_dir.path())) {
                if (!cat_dir.is_directory()) continue;
                std::string category = cat_dir.path().filename().string();
                for (auto& entry : fs::directory_iterator(cat_dir.path())) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
                    auto fname = entry.path().filename().string();
                    auto date = fname.substr(0, fname.size() - 3);
                    s_entries.push_back({
                        fname, date, category, role_name,
                        entry.path().string(),
                        FirstLine(entry.path().string()),
                        ""
                    });
                }
            }
        }
    }

    // 2) Daily notes: ~/.prosophor/memory/*.md
    auto daily_dir = base / "memory";
    if (fs::exists(daily_dir)) {
        for (auto& e : fs::directory_iterator(daily_dir)) {
            if (!e.is_regular_file() || e.path().extension() != ".md") continue;
            auto fname = e.path().filename().string();
            auto date = fname.substr(0, fname.size() - 3);
            s_entries.push_back({
                fname, date, "daily", "",
                e.path().string(),
                FirstLine(e.path().string()),
                ""
            });
        }
    }

    // Sort by date descending (newest first)
    std::sort(s_entries.begin(), s_entries.end(),
        [](const auto& a, const auto& b) { return a.date > b.date; });

    LOG_INFO("ScanAllMemories: found {} entries", s_entries.size());
}

void LoadContent(int idx) {
    if (idx < 0 || idx >= (int)s_entries.size()) return;
    auto& e = s_entries[idx];
    if (e.content.empty()) {
        e.content = ReadFileContent(e.path);
    }
    s_detail_content = e.content;
    s_editing = false;
    s_edit_content.clear();
}

}  // anonymous namespace

// ============================================================================
// ChatWindow::RenderMemoryView
// ============================================================================
void ChatWindow::RenderMemoryView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    auto Lc = LayoutConfig{};
    float sm = Spacing();

    PanelContainer pf(cont_x, cont_y, cont_w, cont_h, L.Get("view_memory").c_str());

    // ── Scan on first render or refresh ──
    if (s_scan) {
        ScanAllMemories();

        // Rebuild role filter list
        s_role_names.clear();
        s_role_cstrs.clear();
        s_role_names.push_back("📋 所有角色");
        for (const auto& e : s_entries) {
            if (!e.role.empty() && std::find(s_role_names.begin() + 1, s_role_names.end(), e.role) == s_role_names.end()) {
                s_role_names.push_back(e.role);
            }
        }
        std::sort(s_role_names.begin() + 1, s_role_names.end());
        if (s_role_filter >= (int)s_role_names.size()) s_role_filter = 0;
        s_role_cstrs.reserve(s_role_names.size());
        for (const auto& n : s_role_names) s_role_cstrs.push_back(n.c_str());

        s_sel = -1;
        s_detail_content.clear();
        s_editing = false;
        s_edit_content.clear();
        s_scan = false;
    }

    float x = static_cast<float>(pf.a.x);
    float y = static_cast<float>(pf.a.y);
    float w = static_cast<float>(pf.a.w);

    // ── Error message banner ──
    if (s_error_timer > 0 && !s_error_msg.empty()) {
        float err_h = 24.0f * sm;
        media_engine::DrawList::RoundRect(x + 4.0f, y + 4.0f, w - 8.0f, err_h, 4.0f,
            media_engine::Colors::RedMid);
        media_engine::DrawList::Text(x + 12.0f, y + 6.0f, media_engine::Colors::White,
            s_error_msg.c_str());
        y += err_h + 4.0f * sm;
        s_error_timer--;
        if (s_error_timer == 0) s_error_msg.clear();
    }

    // ── Search bar + Add Note ──
    float bar_h = 30.0f * sm;
    float bar_y = y + 4.0f;
    float search_x = x + 4.0f;
    float search_w = w * 0.35f;

    // Search input
    s_search_query.resize(256);
    media_engine::Layout::SetCursorScreenPos(search_x, bar_y);
    auto _sw = media_engine::ScopedItemWidth(search_w);
    media_engine::ImGuiWidget::InputText("##mem_search", s_search_query.data(), s_search_query.size());
    s_search_query.resize(std::strlen(s_search_query.data()));

    // Type filter combo
    float filter_x = search_x + search_w + 8.0f * sm;
    float filter_w = 150.0f * sm;
    media_engine::Layout::SetCursorScreenPos(filter_x, bar_y);
    auto _fw = media_engine::ScopedItemWidth(filter_w);
    media_engine::ImGuiWidget::Combo("##mem_filter", &s_type_filter, kTypeLabels, 5);

    // Role filter combo
    float role_x = filter_x + filter_w + 8.0f * sm;
    float role_w = 140.0f * sm;
    if (!s_role_cstrs.empty()) {
        media_engine::Layout::SetCursorScreenPos(role_x, bar_y);
        auto _rw = media_engine::ScopedItemWidth(role_w);
        media_engine::ImGuiWidget::Combo("##mem_role", &s_role_filter, s_role_cstrs.data(), (int)s_role_cstrs.size());
    }

    float note_area_y = bar_y + bar_h + 4.0f * sm;

    // ── Split: list (left) + detail (right) ──
    float content_h = static_cast<float>(pf.a.y + pf.a.h) - note_area_y - 8.0f * sm;
    float left_w = Lc.panel_left_list_w;
    float right_x = x + left_w + 8.0f * sm;
    float right_w = w - left_w - 12.0f * sm;
    float list_top = note_area_y;

    // ── Build filtered list ──
    // If search is active, filter; otherwise show all
    std::vector<int> filtered_indices;
    for (int i = 0; i < (int)s_entries.size(); ++i) {
        auto& e = s_entries[i];
        // Type filter (check content if needed)
        if (!MatchesTypeFilter(e.content.empty() ? e.preview : e.content, s_type_filter))
            continue;
        // Role filter
        if (s_role_filter > 0 && s_role_filter < (int)s_role_names.size()) {
            if (e.role != s_role_names[s_role_filter])
                continue;
        }
        // Search filter
        if (!MatchesSearch(e, s_search_query))
            continue;
        filtered_indices.push_back(i);
    }

    // Validate selection
    if (s_sel >= 0) {
        bool found = false;
        for (int idx : filtered_indices) {
            if (idx == s_sel) { found = true; break; }
        }
        if (!found) s_sel = -1;
    }

    // ── Left: entry list ──
    {
        media_engine::Layout::SetCursorScreenPos(x, list_top);
        auto _l = media_engine::ScopedChild("mem_list", left_w, content_h, 0,
            media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        ItemList mem_list(x, list_top, left_w, sm);
        for (int fi = 0; fi < (int)filtered_indices.size(); ++fi) {
            int idx = filtered_indices[fi];
            auto& e = s_entries[idx];
            std::string label;
            if (!e.role.empty())
                label = "[" + e.role + "] " + e.date;
            else
                label = "[daily] " + e.date;
            if (!e.preview.empty())
                label += "\n" + e.preview;

            if (mem_list.Item(("me" + std::to_string(idx)).c_str(), label.c_str(), idx == s_sel)) {
                s_sel = idx;
                LoadContent(idx);
            }
        }

        if (filtered_indices.empty()) {
            media_engine::Text::Colored(media_engine::Colors::Gray55,
                s_entries.empty() ? L.Get("panel_no_data").c_str() : "无匹配条目");
        }
    }

    // ── Right: detail view ──
    {
        media_engine::Layout::SetCursorScreenPos(right_x, list_top);
        auto _r = media_engine::ScopedChild("mem_detail", right_w, content_h, 0,
            media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (s_sel < 0 || s_sel >= (int)s_entries.size()) {
            media_engine::Text::Colored(media_engine::Colors::Gray55,
                filtered_indices.empty() ? L.Get("panel_no_data").c_str() : "请选择条目");
        } else {
            auto& e = s_entries[s_sel];

            // ── Metadata card ──
            float meta_h = 72.0f;
            float meta_x = right_x;
            float meta_w = right_w - 8.0f * sm;

            media_engine::DrawList::RoundRect(meta_x + 4.0f, list_top + 4.0f, meta_w, meta_h, 6.0f,
                media_engine::Colors::Beige);
            media_engine::DrawList::RoundRectOutline(meta_x + 4.0f, list_top + 4.0f, meta_w, meta_h,
                6.0f, media_engine::Colors::Gray63, 1.0f);

            float my = list_top + 14.0f;
            float lx = meta_x + 12.0f;

            // Title
            media_engine::DrawList::Text(lx, my, media_engine::Colors::OrangeDeep, e.title.c_str());
            my += 22.0f;

            // Source + Role + Date
            std::string meta_line = "来源: " + e.source;
            if (!e.role.empty()) meta_line += " | 角色: " + e.role;
            meta_line += " | " + e.date;
            media_engine::DrawList::Text(lx, my, media_engine::Colors::Gray55, meta_line.c_str());

            // Type badge (scan content for KeyDecision type)
            if (!e.content.empty() || !e.preview.empty()) {
                const std::string& content_ref = e.content.empty() ? e.preview : e.content;
                float badge_x = meta_x + 12.0f;
                float badge_y = my + 20.0f;
                for (int ti = 1; ti <= 4; ++ti) {
                    if (content_ref.find(kTypeKeys[ti]) != std::string::npos) {
                        float bw = 90.0f * sm;
                        media_engine::DrawList::RoundRect(badge_x, badge_y, bw, 18.0f, 9.0f,
                            media_engine::Colors::OrangeLightest);
                        media_engine::DrawList::Text(badge_x + 8.0f, badge_y + 2.0f,
                            media_engine::Colors::OrangeDeep, kTypeLabels[ti] + 4); // skip emoji+space
                        badge_x += bw + 6.0f * sm;
                    }
                }
            }

            // ── Action buttons (top-right of metadata) ──
            float btn_h_act = 28.0f * sm;
            float bx = meta_x + meta_w - 150.0f * sm;
            float by = list_top + 12.0f;
            { // Edit / Cancel
                auto ecol = s_editing ? media_engine::Colors::OrangeLight : media_engine::Colors::BlueLight;
                float bw = 55.0f * sm;
                media_engine::DrawList::RoundRect(bx, by, bw, btn_h_act, 4.0f, ecol);
                media_engine::DrawList::Text(bx + 8.0f, by + 6.0f, media_engine::Colors::White,
                    s_editing ? L.Get("btn_cancel").c_str() : "编辑");
                media_engine::Layout::SetCursorScreenPos(bx, by);
                if (media_engine::ImGuiWidget::InvisibleButton("medit", bw, btn_h_act)) {
                    if (s_editing) {
                        s_editing = false;
                        s_edit_content.clear();
                    } else {
                        s_edit_content = s_detail_content;
                        s_edit_path = e.path;
                        s_editing = true;
                    }
                }
                bx += bw + 4.0f * sm;
            }
            if (s_editing) {
                float bw = 55.0f * sm;
                media_engine::DrawList::RoundRect(bx, by, bw, btn_h_act, 4.0f, media_engine::Colors::Green);
                media_engine::DrawList::Text(bx + 6.0f, by + 6.0f, media_engine::Colors::White,
                    L.Get("btn_save").c_str());
                media_engine::Layout::SetCursorScreenPos(bx, by);
                if (media_engine::ImGuiWidget::InvisibleButton("msave", bw, btn_h_act)) {
                    if (WriteFile(s_edit_path, s_edit_content, false)) {
                        s_detail_content = s_edit_content;
                        e.content = s_edit_content;
                        s_editing = false;
                        s_edit_content.clear();
                    } else {
                        SetError("保存失败");
                    }
                }
                bx += bw + 4.0f * sm;
            }
            { // Delete
                float bw = 55.0f * sm;
                media_engine::DrawList::RoundRect(bx, by, bw, btn_h_act, 4.0f, media_engine::Colors::Red);
                media_engine::DrawList::Text(bx + 8.0f, by + 6.0f, media_engine::Colors::White,
                    L.Get("btn_delete").c_str());
                media_engine::Layout::SetCursorScreenPos(bx, by);
                if (media_engine::ImGuiWidget::InvisibleButton("mdelete", bw, btn_h_act)) {
                    if (fs::remove(e.path)) {
                        s_sel = -1;
                        s_detail_content.clear();
                        s_editing = false;
                        s_edit_content.clear();
                        s_scan = true;
                    } else {
                        SetError("删除失败");
                    }
                }
            }

            // ── Content area ──
            float content_top = list_top + meta_h + 16.0f * sm;
            float content_h2 = content_h - (content_top - list_top) - 8.0f * sm;
            if (content_h2 > 40.0f) {
                if (s_editing) {
                    media_engine::Layout::SetCursorScreenPos(right_x + 8.0f, content_top);
                    auto _cw = media_engine::ScopedItemWidth(right_w - 16.0f * sm);
                    // Dynamic buffer: ensure enough space
                    s_edit_content.resize(std::max(s_edit_content.size() + 1, (size_t)8192));
                    media_engine::ImGuiWidget::InputTextMultiline("##mcont",
                        s_edit_content.data(), s_edit_content.size(),
                        right_w - 16.0f * sm, content_h2, false);
                    s_edit_content.resize(std::strlen(s_edit_content.data()));
                } else {
                    media_engine::DrawList::Text(right_x + 12.0f, content_top,
                        media_engine::Colors::Gray40, s_detail_content.c_str());
                }
            }
        }
    }
}

}  // namespace prosophor
