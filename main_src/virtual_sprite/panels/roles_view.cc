#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/components/item_list.h"
#include "virtual_sprite/layout_config.h"
#include "virtual_sprite/sprite_manager.h"
#include "prosophor_core/public/config.h"
#include "prosophor_core/public/role_config.h"
#include "media_engine/media_engine.h"
#include "common/i18n.h"
#include "common/file_utils.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace prosophor {

namespace {


static std::vector<std::string> all_ids, dnames, avail_models, descrs, prompts, spritesheets;
static std::vector<std::string> s_new_ids; // editable id field
static std::vector<int> checked, model_idx, voice_idx, auto_confirms, enable_tools_flags, max_iters, enable_streams;
static std::vector<int> thinking_flags, thinking_budgets, re_idx;
static std::vector<const char*> m_cstrs, v_cstrs, re_cstrs;
static bool scan = true;
static int s_sel = 0;
static int s_prev_sel = -1;
static std::string s_pending_sel;

void ScanRoles() {
    auto& config = ProsophorConfig::GetInstance();
    all_ids.clear(); dnames.clear(); descrs.clear(); prompts.clear(); spritesheets.clear(); max_iters.clear();
    std::string rd = (ProsophorConfig::BaseDir() / "roles").string();
    if (DirExists(rd)) for (auto& e : std::filesystem::directory_iterator(rd))
        if (e.is_regular_file() && e.path().extension() == ".json") all_ids.push_back(e.path().stem().string());
    std::sort(all_ids.begin(), all_ids.end());
    s_new_ids = all_ids; // editable id starts as copy of file stems
    dnames.resize(all_ids.size()); checked.assign(all_ids.size(), 0);
    descrs.resize(all_ids.size()); prompts.resize(all_ids.size());
    spritesheets.resize(all_ids.size()); max_iters.assign(all_ids.size(), 0);
    auto_confirms.assign(all_ids.size(), 0);
    enable_tools_flags.assign(all_ids.size(), 1);
    enable_streams.assign(all_ids.size(), 1);
    thinking_flags.assign(all_ids.size(), 0);
    thinking_budgets.assign(all_ids.size(), 4096);
    re_idx.assign(all_ids.size(), 1); // default "medium"
    for (size_t i = 0; i < all_ids.size(); ++i) {
        if (std::find(config.default_role.begin(), config.default_role.end(), all_ids[i]) != config.default_role.end()) checked[i] = 1;
        std::string fp = (ProsophorConfig::BaseDir() / "roles" / (all_ids[i] + ".json")).string();
        std::ifstream f(fp); if (f.is_open()) try { auto rj = nlohmann::json::parse(f);
            dnames[i] = rj.value("role_name", all_ids[i]);
            descrs[i] = rj.value("description", "");
            spritesheets[i] = rj.value("spritesheet", "");
            prompts[i] = rj.value("soul", "");
            if (rj.contains("llm") && rj["llm"].is_object()) {
                max_iters[i] = rj["llm"].value("max_iterations", 15);
                enable_tools_flags[i] = rj["llm"].value("enable_tools", true) ? 1 : 0;
                auto_confirms[i] = rj["llm"].value("auto_confirm_tools", false) ? 1 : 0;
                enable_streams[i] = rj["llm"].value("enable_streaming", true) ? 1 : 0;
                thinking_flags[i] = rj["llm"].value("thinking", false) ? 1 : 0;
                thinking_budgets[i] = rj["llm"].value("thinking_budget_tokens", 4096);
                std::string re = rj["llm"].value("reasoning_effort", "medium");
                re_idx[i] = (re == "low") ? 0 : (re == "high") ? 2 : 1;
            }
        } catch(...) { dnames[i] = all_ids[i]; } else dnames[i] = all_ids[i];
    }
    avail_models.clear();
    for (auto& [pn, pv] : config.llm_providers) for (auto& [mn, mc] : pv.model_configs) { std::string d = "[" + pn + "] " + mc.model; if (std::find(avail_models.begin(), avail_models.end(), d) == avail_models.end()) avail_models.push_back(d); }
    m_cstrs.clear(); for (auto& m : avail_models) m_cstrs.push_back(m.c_str());
    v_cstrs.clear(); for (auto& v : config.tts.voice_list) v_cstrs.push_back(v.c_str());
    re_cstrs = {"low", "medium", "high"};
    model_idx.assign(all_ids.size(), 0); voice_idx.assign(all_ids.size(), 0);
    for (size_t i = 0; i < all_ids.size(); ++i) {
        std::string fp = (ProsophorConfig::BaseDir() / "roles" / (all_ids[i] + ".json")).string();
        std::ifstream f(fp); if (!f.is_open()) continue;
        try { auto rj = nlohmann::json::parse(f);
            std::string rm = rj["llm"]["model"];
            for (size_t ai = 0; ai < avail_models.size(); ++ai) { auto be = avail_models[ai].find("] "); if (be != std::string::npos && avail_models[ai].substr(be + 2) == rm) { model_idx[i] = (int)ai; break; } }
            if (rj.contains("tts") && rj["tts"].is_object()) { std::string v = rj["tts"].value("voice",""); for (size_t vi = 0; vi < config.tts.voice_list.size(); ++vi) { if (config.tts.voice_list[vi] == v) { voice_idx[i] = (int)vi; break; } } }
        } catch(...) {}
    }
    scan = false;
}

} // anonymous namespace

void ChatWindow::RenderRolesView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto Lc = LayoutConfig{};
    auto& L = I18n::Instance();
    PanelContainer pf(cont_x, cont_y, cont_w, cont_h, L.Get("view_roles").c_str());

    auto& config = ProsophorConfig::GetInstance();
    float s = Spacing();
    if (scan) {
        ScanRoles();
        if (!s_pending_sel.empty()) {
            auto it = std::find(all_ids.begin(), all_ids.end(), s_pending_sel);
            if (it != all_ids.end()) s_sel = (int)(it - all_ids.begin());
            s_pending_sel.clear();
        }
    }

    float btn_h = 30.0f, gap = 12.0f;
    int vc = (int)v_cstrs.size();
    float left_w = Lc.panel_left_list_w;

    auto sv = SplitPanel(pf.a, left_w, btn_h, gap);

    if (s_sel >= (int)all_ids.size()) s_sel = 0;
    if (s_sel != s_prev_sel) {
        s_prev_sel = s_sel;
    }

    // ── Left: role list ──
    {
        media_engine::Layout::SetCursorScreenPos(sv.left_x, sv.left_y);
        auto _l = media_engine::ScopedChild("rl_list", sv.left_w, sv.inner_h, 0,
            media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        {
            ItemList list(sv.left_x, sv.left_y, sv.left_w, s);
            for (size_t i = 0; i < all_ids.size(); ++i) {
                bool ck = (checked[i] != 0);
                std::string label = all_ids[i];
                if (list.Item(("rl_" + all_ids[i]).c_str(),
                        label.c_str(), (int)i == s_sel, &ck))
                    s_sel = (int)i;
                checked[i] = ck ? 1 : 0;
            }
        }
    }

    sv.DrawDivider();

    // ── Right: selected role content ──
    {
        media_engine::Layout::SetCursorScreenPos(sv.right_x, sv.right_y);
        auto _r = media_engine::ScopedChild("rl_cfg", sv.right_w, sv.inner_h,
            0, media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (all_ids.empty()) {
            media_engine::Text::Colored(media_engine::Colors::Gray55, L.Get("panel_no_data").c_str());
            goto buttons;
        }

        size_t i = (size_t)s_sel;
        std::string role_id = all_ids[i];

        // ── Role config ──
        float cx = sv.right_x;
        float cw = sv.right_w - Lc.section_card_right_margin + Lc.split_right_child_wextra;

        float cX, cY;
        media_engine::Layout::GetCursorScreenPos(&cX, &cY);
        cY += 6.0f * s;
        media_engine::Layout::SetCursorScreenPos(cX, cY);
        {
            Card role_card(cx, cY, cw, s_new_ids[i].c_str(), s);

        // id
        s_new_ids[i].resize(256);
        role_card.Field("id", [&](){
            media_engine::ImGuiWidget::InputText(("##id_" + all_ids[i]).c_str(), s_new_ids[i].data(), s_new_ids[i].size()); });
        s_new_ids[i].resize(std::strlen(s_new_ids[i].data()));

        // role_name
        dnames[i].resize(256);
        role_card.Field("role_name", [&](){
            media_engine::ImGuiWidget::InputText(("##rn_" + all_ids[i]).c_str(), dnames[i].data(), dnames[i].size()); });
        dnames[i].resize(std::strlen(dnames[i].data()));

        // spritesheet
        spritesheets[i].resize(256);
        role_card.Field("spritesheet", [&](){
            media_engine::ImGuiWidget::InputText(("##sp_" + all_ids[i]).c_str(), spritesheets[i].data(), spritesheets[i].size()); });
        spritesheets[i].resize(std::strlen(spritesheets[i].data()));

        float mid_x = role_card.ContentX() + Lc.label_row_pad;
        float iw = (cx + cw) - mid_x - 24.0f;

        // description
        descrs[i].resize(1024);
        media_engine::Layout::SetCursorScreenPos(mid_x, role_card.Y());
        media_engine::Text::Colored(media_engine::Colors::Gray55, "description");
        role_card.Advance(22.0f * s);
        media_engine::Layout::SetCursorScreenPos(mid_x, role_card.Y());
        media_engine::ImGuiWidget::InputTextMultiline(("##desc_" + all_ids[i]).c_str(), descrs[i].data(), descrs[i].size(), iw, 50.0f * s, false);
        descrs[i].resize(std::strlen(descrs[i].data()));
        role_card.Advance(60.0f * s);

        // soul
        prompts[i].resize(4096);
        media_engine::Layout::SetCursorScreenPos(mid_x, role_card.Y());
        media_engine::Text::Colored(media_engine::Colors::Gray55, "soul");
        role_card.Advance(22.0f * s);
        media_engine::Layout::SetCursorScreenPos(mid_x, role_card.Y());
        media_engine::ImGuiWidget::InputTextMultiline(("##prompt_" + all_ids[i]).c_str(), prompts[i].data(), prompts[i].size(), iw, 70.0f * s, false);
        prompts[i].resize(std::strlen(prompts[i].data()));
        role_card.Advance(80.0f * s);

        // auto_confirm_tools
        bool ac = (auto_confirms[i] != 0);
        role_card.Field("auto_confirm_tools", [&](){
            media_engine::ImGuiWidget::Checkbox(("##autoconf_" + all_ids[i]).c_str(), &ac); });
        auto_confirms[i] = ac ? 1 : 0;

        // enable_tools
        bool et = (enable_tools_flags[i] != 0);
        role_card.Field("enable_tools", [&](){
            media_engine::ImGuiWidget::Checkbox(("##et_" + all_ids[i]).c_str(), &et); });
        enable_tools_flags[i] = et ? 1 : 0;

        // enable_streaming
        bool es = (enable_streams[i] != 0);
        role_card.Field("enable_streaming", [&](){
            media_engine::ImGuiWidget::Checkbox(("##es_" + all_ids[i]).c_str(), &es); });
        enable_streams[i] = es ? 1 : 0;

        // thinking
        bool tf = (thinking_flags[i] != 0);
        role_card.Field("thinking", [&](){
            media_engine::ImGuiWidget::Checkbox(("##thk_" + all_ids[i]).c_str(), &tf); });
        thinking_flags[i] = tf ? 1 : 0;

        // thinking_budget_tokens
        role_card.Field("thinking_budget_tokens", [&,i](){
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", thinking_budgets[i]);
            media_engine::ImGuiWidget::InputText(("##tbt_" + all_ids[i]).c_str(), buf, sizeof(buf));
            int v = atoi(buf);
            if (v > 0) thinking_budgets[i] = v;
        });

        // reasoning_effort
        role_card.Field("reasoning_effort", [&](){
            media_engine::ImGuiWidget::Combo(("##re_" + all_ids[i]).c_str(), &re_idx[i], re_cstrs.data(), (int)re_cstrs.size()); });

        // max_iterations
        role_card.Field("max_iterations", [&,i](){
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", max_iters[i]);
            media_engine::ImGuiWidget::InputText(("##mi_" + all_ids[i]).c_str(), buf, sizeof(buf));
            int v = atoi(buf);
            if (v > 0) max_iters[i] = v;
        });

        // model
        role_card.Field("model", [&](){
            float cw_half = ((cx + cw) - role_card.WidgetX()) * 0.5f;
            auto _w = media_engine::ScopedItemWidth(cw_half);
            media_engine::ImGuiWidget::Combo(("##mdl_" + all_ids[i]).c_str(), &model_idx[i], m_cstrs.data(), (int)m_cstrs.size()); });

        // tts voice
        if (vc > 0) {
            role_card.Field("voice", [&](){
                float cw_half = ((cx + cw) - role_card.WidgetX()) * 0.5f;
                auto _w = media_engine::ScopedItemWidth(cw_half);
                media_engine::ImGuiWidget::Combo(("##voi_" + all_ids[i]).c_str(), &voice_idx[i], v_cstrs.data(), vc); });
        }
    }
}
buttons:
    float by = pf.a.y + pf.a.h - btn_h - 4.0f;
    float btn_w = Lc.panel_save_btn_w, btn_d = Lc.panel_btn_gap;

    // "New Role" button on the left edge of the content area
    float new_bx = pf.a.x;
    float new_btn_w = Lc.panel_left_list_w;
    media_engine::Layout::SetCursorScreenPos(new_bx, by);
    if (media_engine::ImGuiWidget::Button(L.Get("btn_new_role").c_str(), new_btn_w, 0)) {
        auto rd = ProsophorConfig::BaseDir() / "roles";
        // Generate a unique role ID
        std::string base = "new_role";
        std::string new_id = base;
        for (int n = 1; std::filesystem::exists(rd / (new_id + ".json")); ++n)
            new_id = base + "_" + std::to_string(n);
        // Write default JSON template
        nlohmann::ordered_json j;
        j["role_name"] = new_id;
        j["description"] = "";
        j["soul"] = "";
        j["llm"]["enable_tools"] = true;
        j["llm"]["enable_streaming"] = true;
        j["llm"]["max_iterations"] = 15;
        j["llm"]["auto_confirm_tools"] = false;
        WriteOrderedJson((rd / (new_id + ".json")).string(), j, 2);
        s_pending_sel = new_id;
        scan = true;
    }

    float bx = pf.a.x + pf.a.w - (btn_w * 3 + btn_d * 2) - Lc.panel_btn_right_gap;
    media_engine::Layout::SetCursorScreenPos(bx, by);
    if (media_engine::ImGuiWidget::Button(L.Get("btn_save").c_str(), btn_w, 0)) try {
        std::vector<std::string> nr;
        for (size_t i = 0; i < all_ids.size(); ++i) {
            if (checked[i]) nr.push_back(all_ids[i]);
            // Save config for ALL roles, not just checked ones
            auto d = avail_models[model_idx[i]]; auto be = d.find("] ");
            if (be != std::string::npos) {
                std::string p = d.substr(1, be - 1), m = d.substr(be + 2);
                RoleConfigManager::SaveModel(all_ids[i], p, m); RoleConfigManager::HotSwitch(all_ids[i], p, m);
            }
            if (vc > 0 && voice_idx[i] >= 0 && voice_idx[i] < vc) {
                std::string v = config.tts.voice_list[voice_idx[i]];
                RoleConfigManager::SaveTtsVoice(all_ids[i], v, config.tts.backend);
                RoleConfigManager::HotSwitchTtsVoice(all_ids[i], v, config.tts.backend);
            }
            RoleConfigManager::SaveField(all_ids[i], "role_name", dnames[i]);
            RoleConfigManager::SaveField(all_ids[i], "spritesheet", spritesheets[i]);
            RoleConfigManager::SaveField(all_ids[i], "description", descrs[i]);
            RoleConfigManager::SaveField(all_ids[i], "soul", prompts[i]);
            RoleConfigManager::SaveFieldInt(all_ids[i], "llm.max_iterations", max_iters[i]);
            RoleConfigManager::SaveFieldBool(all_ids[i], "llm.enable_tools", enable_tools_flags[i] != 0);
            RoleConfigManager::SaveFieldBool(all_ids[i], "llm.auto_confirm_tools", auto_confirms[i] != 0);
            RoleConfigManager::SaveFieldBool(all_ids[i], "llm.enable_streaming", enable_streams[i] != 0);
            RoleConfigManager::SaveFieldBool(all_ids[i], "llm.thinking", thinking_flags[i] != 0);
            RoleConfigManager::SaveFieldInt(all_ids[i], "llm.thinking_budget_tokens", thinking_budgets[i]);
            static const char* re_vals[] = {"low", "medium", "high"};
            RoleConfigManager::SaveField(all_ids[i], "llm.reasoning_effort", re_vals[re_idx[i]]);
        }
        // Handle role ID renames (file rename on disk)
        {
            auto rd = ProsophorConfig::BaseDir() / "roles";
            for (size_t i = 0; i < all_ids.size(); ++i) {
                std::string new_id = s_new_ids[i];
                // Trim whitespace
                new_id.erase(0, new_id.find_first_not_of(" \t\r\n"));
                new_id.erase(new_id.find_last_not_of(" \t\r\n") + 1);
                if (new_id.empty() || new_id == all_ids[i]) continue;
                auto old_path = rd / (all_ids[i] + ".json");
                auto new_path = rd / (new_id + ".json");
                if (std::filesystem::exists(old_path) && !std::filesystem::exists(new_path)) {
                    // Read old JSON, update "id" field, write to new path
                    auto content = ReadFile(old_path.string());
                    if (content) {
                        nlohmann::ordered_json j = nlohmann::ordered_json::parse(*content);
                        j["id"] = new_id;
                        WriteOrderedJson(new_path.string(), j, 2);
                    }
                    std::filesystem::remove(old_path);
                    // Update default_role list
                    auto dit = std::find(nr.begin(), nr.end(), all_ids[i]);
                    if (dit != nr.end()) *dit = new_id;
                    // Update in-memory id for the sprite/scan phases below
                    all_ids[i] = new_id;
                }
            }
        }
        // Queue sprite create/remove — executed in next update tick (safe)
        auto& old_roles = config.default_role;
        for (size_t i = 0; i < all_ids.size(); ++i) {
            bool was = std::find(old_roles.begin(), old_roles.end(), all_ids[i]) != old_roles.end();
            bool now = checked[i] != 0;
            if (!was && now)
                SpriteManager::GetInstance().QueueCreateSprite(all_ids[i]);
            else if (was && !now)
                SpriteManager::GetInstance().RemoveSpriteByRoleId(all_ids[i]);
        }
        config.default_role = nr; config.SaveToFile(); scan = true;
        // HotReload ALL roles that have running sessions
        for (size_t i = 0; i < all_ids.size(); ++i) {
            RoleConfigManager::HotReload(all_ids[i]);
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[roles_view] Save failed: %s\n", e.what());
    }
    media_engine::Layout::SameLine(); media_engine::Layout::Dummy(btn_d, 0); media_engine::Layout::SameLine();
    if (!all_ids.empty()) {
        std::string sel_id = all_ids[s_sel];
        if (media_engine::ImGuiWidget::Button(L.Get("btn_delete").c_str(), btn_w, 0)) {
                auto rd = ProsophorConfig::BaseDir() / "roles";
                std::filesystem::remove(rd / (sel_id + ".json"));
                if (checked[s_sel]) {
                    auto& old_roles = config.default_role;
                    auto it = std::find(old_roles.begin(), old_roles.end(), sel_id);
                    if (it != old_roles.end()) {
                        old_roles.erase(it);
                        config.SaveToFile();
                    }
                    SpriteManager::GetInstance().RemoveSpriteByRoleId(sel_id);
                }
                s_sel = 0;
                scan = true;
        }
        media_engine::Layout::SameLine(); media_engine::Layout::Dummy(btn_d, 0); media_engine::Layout::SameLine();
    }
    if (media_engine::ImGuiWidget::Button(L.Get("nav_refresh").c_str(), btn_w, 0)) scan = true;
}

} // namespace prosophor
