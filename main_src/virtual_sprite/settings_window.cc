// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/settings_window.h"
#include "common/i18n.h"
#include "common/log_wrapper.h"
#include "common/file_utils.h"
#include "prosophor_core/public/config.h"
#include "prosophor_core/public/role_config.h"
#include "virtual_sprite/voice_engine.h"
#include "media_engine/media/texture/imgui_widget.h"
#include "platform/platform.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace prosophor {

// ========================================================================
// Settings UI helpers — thin wrappers over media_engine widgets
// ========================================================================
namespace {

void SettingLabel(const char* label) {
    media_engine::Text::Raw(label);
    media_engine::Layout::SameLine();
}

bool ComboSetting(const char* id, const char* const* items, int count, int& idx) {
    return media_engine::ImGuiWidget::Combo(id, &idx, items, count);
}

bool CheckboxSetting(const char* id, bool& val) {
    return media_engine::ImGuiWidget::Checkbox(id, &val);
}

void InputTextSetting(const char* id, char* buf, size_t buf_size) {
    media_engine::ImGuiWidget::InputText(id, buf, buf_size);
}

void InputIntSetting(const char* id, int& val) {
    media_engine::ImGuiWidget::InputInt(id, &val);
}

void Spacer() {
    media_engine::Layout::Dummy(0, 8);
}

}  // namespace

// ========================================================================
// SettingsState
// ========================================================================

struct SettingsWindow::SettingsState {
    bool first_open = true;

    // General
    std::string log_level;
    bool enable_summary = false;
    std::string sprite_dir;

    // Roles
    std::vector<std::string> edit_roles;
    std::vector<int> role_checked;
    std::vector<std::string> all_role_ids;
    std::vector<std::string> available_models;
    std::vector<int> role_model_idx;
    std::vector<int> role_voice_idx;
    std::vector<std::string> role_names;

    // Security
    std::string perm_level;
    bool allow_local_exec = false;

    // TTS
    bool tts_enabled = false;
    std::string tts_backend;
    std::string edge_voice;
    bool edge_auto_start = false;
    char test_text[256]{};
};

SettingsWindow::SettingsWindow() = default;
SettingsWindow::~SettingsWindow() = default;

// ========================================================================
// Render — thin shell: modal window + tab routing + save/cancel
// ========================================================================

void SettingsWindow::Render() {
    if (!open_) return;

    if (!s_) {
        s_ = std::make_unique<SettingsState>();
    }

    auto& L = I18n::Instance();

    auto _colors = media_engine::ScopedColors(
        media_engine::Color::Slot::TitleBg, media_engine::Colors::BluePale)
        .Then(media_engine::Color::Slot::TitleBgActive, media_engine::Colors::BlueLightSoft)
        .Then(media_engine::Color::Slot::WindowBg, media_engine::Colors::White)
        .Then(media_engine::Color::Slot::ChildBg, media_engine::Colors::White)
        .Then(media_engine::Color::Slot::Text, media_engine::Colors::Gray35)
        .Then(media_engine::Color::Slot::FrameBg, media_engine::Colors::Gray95)
        .Then(media_engine::Color::Slot::FrameBgHovered, media_engine::Colors::Gray86)
        .Then(media_engine::Color::Slot::FrameBgActive, media_engine::Colors::Gray78)
        .Then(media_engine::Color::Slot::PopupBg, media_engine::Colors::White)
        .Then(media_engine::Color::Slot::Border, media_engine::Colors::Gray63)
        .Then(media_engine::Color::Slot::CheckMark, media_engine::Colors::BlueSoft)
        .Then(media_engine::Color::Slot::Tab, media_engine::Colors::Gray90)
        .Then(media_engine::Color::Slot::TabHovered, media_engine::Colors::BlueLight)
        .Then(media_engine::Color::Slot::TabActive, media_engine::Colors::White)
        .Then(media_engine::Color::Slot::TabUnfocused, media_engine::Colors::Gray86)
        .Then(media_engine::Color::Slot::TabUnfocusedActive, media_engine::Colors::White)
        .Then(media_engine::Color::Slot::Header, media_engine::Colors::BluePale)
        .Then(media_engine::Color::Slot::HeaderHovered, media_engine::Colors::BlueLight)
        .Then(media_engine::Color::Slot::HeaderActive, media_engine::Colors::BlueLightSoft);

    auto _border = media_engine::ScopedStyleVar::FrameBorderSize(1.0f);

    media_engine::Popup::Open("settings_modal");
    media_engine::ImGuiWindow::SetNextSize(560.0f, 460.0f, media_engine::ImGuiCond_Appearing);
    auto _set = media_engine::ScopedModal(
        (L.Get("settings_title") + "###settings_modal").c_str(), &open_,
        media_engine::ImGuiWindowFlags_NoSavedSettings);
    if (!_set) return;

    if (s_->first_open) {
        InitState();
        s_->first_open = false;
    }

    if (auto _bar = media_engine::ScopedTabBar("SettingsTabs")) {
        if (auto _tab = media_engine::ScopedTabItem(L.Get("tab_general").c_str())) {
            RenderGeneralTab();
        }
        if (auto _tab = media_engine::ScopedTabItem(L.Get("tab_roles").c_str())) {
            RenderRolesTab();
        }
        if (auto _tab = media_engine::ScopedTabItem(L.Get("tab_providers").c_str())) {
            RenderProvidersTab();
        }
        if (auto _tab = media_engine::ScopedTabItem(L.Get("tab_security").c_str())) {
            RenderSecurityTab();
        }
        if (auto _tab = media_engine::ScopedTabItem(L.Get("tab_tts").c_str())) {
            RenderTtsTab();
        }
        if (auto _tab = media_engine::ScopedTabItem(L.Get("tab_local_models").c_str())) {
            RenderLocalModelsTab();
        }

    }

    media_engine::ImGuiWidget::Separator();
    media_engine::Layout::Dummy(0, 4);
    float btn_w = 100.0f;
    float spacing = 8.0f;
    media_engine::Layout::SetCursorPosX(
        media_engine::ImGuiWindow::GetWidth() - btn_w * 2.0f - spacing - 12.0f);

    if (media_engine::ImGuiWidget::Button(L.Get("btn_cancel").c_str(), btn_w, 0)) {
        auto& config = ProsophorConfig::GetInstance();
        config = ProsophorConfig::LoadFromFile(ProsophorConfig::DefaultConfigPath());
        open_ = false;
        s_->first_open = true;
    }
    media_engine::Layout::SameLine();
    if (media_engine::ImGuiWidget::Button(L.Get("btn_save").c_str(), btn_w, 0)) {
        SaveSettings();
        open_ = false;
        s_->first_open = true;
    }
}

// ========================================================================
// InitState — populate editors from current config
// ========================================================================

void SettingsWindow::InitState() {
    auto& config = ProsophorConfig::GetInstance();

    s_->log_level = config.log_level;
    s_->enable_summary = config.enable_summary;
    s_->sprite_dir = config.sprite_assets_dir;
    s_->edit_roles = config.default_role;

    s_->perm_level = config.security.permission_level;
    s_->allow_local_exec = config.security.allow_local_execute;

    s_->tts_enabled = config.tts.enabled;
    s_->tts_backend = config.tts.backend;
    s_->edge_voice = config.tts.voice.empty() ? "zh-CN-XiaoxiaoNeural" : config.tts.voice;
    s_->edge_auto_start = config.tts.gs_auto_start;

    if (!config.llamacpp_models.empty()) {
        edit_lm_port_ = config.llamacpp_models[0].port;
        edit_lm_auto_start_ = config.llamacpp_models[0].auto_start;
        edit_lm_start_timeout_ = config.llamacpp_models[0].start_timeout_ms;
        edit_lm_model_path_ = config.llamacpp_models[0].model_path;
    }

    // Scan available roles
    s_->all_role_ids.clear();
    std::string roles_dir = (ProsophorConfig::BaseDir() / "roles").string();
    if (DirExists(roles_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(roles_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                s_->all_role_ids.push_back(entry.path().stem().string());
            }
        }
    }
    s_->role_checked.assign(s_->all_role_ids.size(), 0);
    for (size_t i = 0; i < s_->all_role_ids.size(); ++i) {
        if (std::find(s_->edit_roles.begin(), s_->edit_roles.end(), s_->all_role_ids[i])
            != s_->edit_roles.end()) {
            s_->role_checked[i] = 1;
        }
    }

    // Collect available models
    s_->available_models.clear();
    for (const auto& [pname, prov] : config.llm_providers) {
        for (const auto& [model_name, model_config] : prov.model_configs) {
            std::string display = "[" + pname + "] " + model_config.model;
            if (std::find(s_->available_models.begin(), s_->available_models.end(), display)
                == s_->available_models.end()) {
                s_->available_models.push_back(display);
            }
        }
    }

    // Read each role's current model, TTS voice, and display name
    s_->role_model_idx.assign(s_->all_role_ids.size(), 0);
    s_->role_voice_idx.assign(s_->all_role_ids.size(), 0);
    s_->role_names.assign(s_->all_role_ids.size(), "");
    const auto& voice_list = config.tts.voice_list;
    for (size_t i = 0; i < s_->all_role_ids.size(); ++i) {
        std::string rp = (ProsophorConfig::BaseDir() / "roles" / (s_->all_role_ids[i] + ".json")).string();
        std::ifstream f(rp);
        if (!f.is_open()) continue;
        try {
            auto rj = nlohmann::json::parse(f);

            // Display name
            s_->role_names[i] = rj.value("role_name", s_->all_role_ids[i]);

            // Model
            std::string rm = rj["llm"]["model"];
            std::string rp_proto = rj["llm"]["protocal"];
            for (size_t ai = 0; ai < s_->available_models.size(); ++ai) {
                auto bracket_end = s_->available_models[ai].find("] ");
                if (bracket_end != std::string::npos
                    && s_->available_models[ai].substr(bracket_end + 2) == rm) {
                    s_->role_model_idx[i] = static_cast<int>(ai);
                    break;
                }
            }
            if (s_->role_model_idx[i] == 0 && !s_->available_models.empty()) {
                for (size_t ai = 0; ai < s_->available_models.size(); ++ai) {
                    auto bracket_end = s_->available_models[ai].find("] ");
                    if (bracket_end != std::string::npos
                        && s_->available_models[ai].substr(1, bracket_end - 1) == rp_proto) {
                        s_->role_model_idx[i] = static_cast<int>(ai);
                        break;
                    }
                }
            }

            // TTS voice
            if (rj.contains("tts") && rj["tts"].is_object()) {
                std::string voice = rj["tts"].value("voice", "");
                for (size_t vi = 0; vi < voice_list.size(); ++vi) {
                    if (voice_list[vi] == voice) {
                        s_->role_voice_idx[i] = static_cast<int>(vi);
                        break;
                    }
                }
            }
        } catch (...) {}
    }
}

// ========================================================================
// Tab renderers
// ========================================================================

void SettingsWindow::RenderGeneralTab() {
    auto& L = I18n::Instance();

    const char* levels[] = {"trace", "debug", "info", "warn", "error"};
    int ll_idx = 0;
    for (int i = 0; i < 5; ++i) {
        if (s_->log_level == levels[i]) { ll_idx = i; break; }
    }
    SettingLabel(L.Get("general_log_level").c_str());
    ComboSetting("##log_level", levels, 5, ll_idx);
    s_->log_level = levels[ll_idx];

    Spacer();
    SettingLabel(L.Get("general_enable_summary").c_str());
    CheckboxSetting("##enable_summary", s_->enable_summary);

    Spacer();
    SettingLabel(L.Get("general_sprite_assets_dir").c_str());
    char dir_buf[512];
    std::strncpy(dir_buf, s_->sprite_dir.c_str(), sizeof(dir_buf) - 1);
    dir_buf[sizeof(dir_buf) - 1] = '\0';
    InputTextSetting("##sprite_dir", dir_buf, sizeof(dir_buf));
    s_->sprite_dir = dir_buf;
    media_engine::Layout::SameLine();
    if (media_engine::ImGuiWidget::Button("...", 30, 0)) {
        auto sel = Platform::BrowseForDirectory();
        if (!sel.empty()) s_->sprite_dir = sel;
    }
}

void SettingsWindow::RenderRolesTab() {
    auto& L = I18n::Instance();

    media_engine::Text::Raw(L.Get("roles_select_hint").c_str());
    media_engine::ImGuiWidget::Separator();
    const auto& voice_list = ProsophorConfig::GetInstance().tts.voice_list;
    std::vector<const char*> voice_cstrs;
    for (const auto& v : voice_list) voice_cstrs.push_back(v.c_str());
    int voice_count = static_cast<int>(voice_cstrs.size());

    for (size_t i = 0; i < s_->all_role_ids.size(); ++i) {
        bool checked = (s_->role_checked[i] != 0);
        std::string label = s_->all_role_ids[i];
        if (!s_->role_names[i].empty() && s_->role_names[i] != label)
            label += " - " + s_->role_names[i];
        CheckboxSetting(label.c_str(), checked);
        s_->role_checked[i] = checked ? 1 : 0;

        media_engine::Layout::Dummy(24.0f, 0.0f);
        media_engine::Layout::SameLine();
        SettingLabel("Model");
        std::vector<const char*> model_cstrs;
        for (const auto& m : s_->available_models) model_cstrs.push_back(m.c_str());
        ComboSetting(("##model_" + s_->all_role_ids[i]).c_str(),
                     model_cstrs.data(), static_cast<int>(model_cstrs.size()),
                     s_->role_model_idx[i]);

        media_engine::Layout::Dummy(24.0f, 0.0f);
        media_engine::Layout::SameLine();
        SettingLabel("Voice");
        ComboSetting(("##voice_" + s_->all_role_ids[i]).c_str(),
                     voice_cstrs.data(), voice_count, s_->role_voice_idx[i]);
    }
}

void SettingsWindow::RenderProvidersTab() {
    auto& config = ProsophorConfig::GetInstance();
    for (auto& [pname, prov] : config.llm_providers) {
        if (!media_engine::ImGuiWidget::TreeNode(pname.c_str())) continue;
        if (prov.entries.empty()) {
            media_engine::Text::Raw("(legacy format, no editable entries)");
        } else {
            for (size_t ei = 0; ei < prov.entries.size(); ++ei) {
                auto& entry = prov.entries[ei];
                std::string label = "Entry " + std::to_string(ei + 1);
                if (!media_engine::ImGuiWidget::TreeNode(label.c_str())) continue;
                auto id = [&](const char* suf) {
                    return ("##" + pname + "_" + std::to_string(ei) + "_" + suf);
                };
                char buf[1024];
                std::strncpy(buf, entry.api_key.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
                SettingLabel("API Key");
                InputTextSetting(id("api").c_str(), buf, sizeof(buf));
                entry.api_key = buf;

                std::strncpy(buf, entry.base_url.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
                SettingLabel("Base URL");
                InputTextSetting(id("url").c_str(), buf, sizeof(buf));
                entry.base_url = buf;

                SettingLabel("Timeout (s)");
                InputIntSetting(id("to").c_str(), entry.timeout);

                if (!entry.models.empty()) {
                    media_engine::ImGuiWidget::Separator();
                    media_engine::Text::Raw("Models");
                    std::vector<std::string> model_keys;
                    for (auto& [mk, mv] : entry.models) model_keys.push_back(mk);
                    for (size_t ai = 0; ai < model_keys.size(); ++ai) {
                        auto& agent = entry.models[model_keys[ai]];
                        if (!media_engine::ImGuiWidget::TreeNode(agent.model.c_str())) continue;
                        auto pid = [&](const char* suf) {
                            return ("##" + pname + "_" + std::to_string(ei) + "_"
                                    + std::to_string(ai) + "_" + suf);
                        };
                        char mbuf[256];
                        std::strncpy(mbuf, agent.model.c_str(), sizeof(mbuf) - 1); mbuf[sizeof(mbuf) - 1] = '\0';
                        SettingLabel("Model");
                        InputTextSetting(pid("model").c_str(), mbuf, sizeof(mbuf));
                        agent.model = mbuf;

                        SettingLabel("Temperature");
                        media_engine::ImGuiWidget::SliderFloat(pid("temp").c_str(), &agent.temperature, 0.0f, 2.0f, "%.1f");
                        SettingLabel("Max Tokens");
                        InputIntSetting(pid("maxtok").c_str(), agent.max_tokens);
                        SettingLabel("Context Window");
                        InputIntSetting(pid("ctx").c_str(), agent.context_window);
                        SettingLabel("Thinking");
                        CheckboxSetting(pid("think").c_str(), agent.thinking);
                        SettingLabel("enable_streaming");
                        CheckboxSetting(pid("stream").c_str(), agent.enable_streaming);
                        media_engine::ImGuiWidget::TreePop();
                    }
                }
                media_engine::ImGuiWidget::TreePop();
            }
            media_engine::Layout::Dummy(0, 4);
            if (media_engine::ImGuiWidget::Button("+ Add Entry", 0, 0)) {
                LlmProviderEntryConfig new_entry;
                new_entry.timeout = 30;
                prov.entries.push_back(std::move(new_entry));
            }
        }
        media_engine::ImGuiWidget::TreePop();
    }
}

void SettingsWindow::RenderSecurityTab() {
    auto& L = I18n::Instance();

    const char* perm_levels[] = {"auto", "allow", "deny"};
    int pl_idx = 0;
    for (int i = 0; i < 3; ++i) {
        if (s_->perm_level == perm_levels[i]) { pl_idx = i; break; }
    }
    SettingLabel(L.Get("security_permission_level").c_str());
    ComboSetting("##perm_level", perm_levels, 3, pl_idx);
    s_->perm_level = perm_levels[pl_idx];

    Spacer();
    SettingLabel("allow_local_execute");
    CheckboxSetting("##allow_local_exec", s_->allow_local_exec);
}

void SettingsWindow::RenderTtsTab() {
    auto& L = I18n::Instance();

    SettingLabel(L.Get("tts_enabled").c_str());
    CheckboxSetting("##tts_enabled", s_->tts_enabled);

    Spacer();
    SettingLabel(L.Get("tts_backend").c_str());
    const char* backends[] = {"edge-tts"};
    int be_idx = 0;
    ComboSetting("##tts_backend", backends, 1, be_idx);

    if (s_->tts_backend == "edge-tts") {
        media_engine::ImGuiWidget::Separator();
        media_engine::Text::Raw("edge-tts");
        Spacer();

        const auto& voice_list = ProsophorConfig::GetInstance().tts.voice_list;
        std::vector<const char*> voice_cstrs;
        for (const auto& v : voice_list) voice_cstrs.push_back(v.c_str());
        int voice_count = static_cast<int>(voice_cstrs.size());
        int voice_idx = 0;
        for (int i = 0; i < voice_count; ++i) {
            if (s_->edge_voice == voice_list[i]) { voice_idx = i; break; }
        }
        SettingLabel("Voice");
        ComboSetting("##edge_voice", voice_cstrs.data(), voice_count, voice_idx);
        if (voice_idx >= 0 && voice_idx < voice_count)
            s_->edge_voice = voice_list[voice_idx];

        Spacer();
        SettingLabel("gs_auto_start");
        CheckboxSetting("##edge_auto_start", s_->edge_auto_start);

        Spacer();
        media_engine::ImGuiWidget::Separator();
        media_engine::Text::Raw("Test");
        InputTextSetting("##tts_test_text", s_->test_text, sizeof(s_->test_text));
        media_engine::Layout::SameLine();
        if (media_engine::ImGuiWidget::Button("Speak", 52.0f, 0)) {
            if (std::strlen(s_->test_text) > 0) {
                VoiceEngine::GetInstance().Speak(s_->test_text, s_->tts_backend, s_->edge_voice);
            }
        }
    }
}

void SettingsWindow::RenderLocalModelsTab() {
    auto& L = I18n::Instance();
    auto& config = ProsophorConfig::GetInstance();
    if (config.llamacpp_models.empty()) {
        media_engine::Text::Raw(L.Get("providers_readonly").c_str());
        return;
    }

    char model_buf[512];
    std::strncpy(model_buf, edit_lm_model_path_.c_str(), sizeof(model_buf) - 1);
    model_buf[sizeof(model_buf) - 1] = '\0';
    SettingLabel(L.Get("local_model_model_path").c_str());
    InputTextSetting("##lm_model_path", model_buf, sizeof(model_buf));
    edit_lm_model_path_ = model_buf;
    media_engine::Layout::SameLine();
    if (media_engine::ImGuiWidget::Button("...##lm_browse", 36, 0)) {
        auto p = Platform::BrowseForFile("GGUF Model (*.gguf)\0*.gguf\0All Files (*.*)\0*.*\0");
        if (!p.empty()) edit_lm_model_path_ = p;
    }

    Spacer();
    SettingLabel(L.Get("local_model_auto_start").c_str());
    CheckboxSetting("##lm_auto_start", edit_lm_auto_start_);

    Spacer();
    SettingLabel(L.Get("local_model_port").c_str());
    InputIntSetting("##lm_port", edit_lm_port_);

    Spacer();
    SettingLabel("start_timeout_ms");
    InputIntSetting("##lm_start_timeout", edit_lm_start_timeout_);
}

// ========================================================================
// SaveSettings — write edit state back to config
// ========================================================================

void SettingsWindow::SaveSettings() {
    auto& config = ProsophorConfig::GetInstance();

    // General
    config.log_level = s_->log_level;
    config.enable_summary = s_->enable_summary;
    config.sprite_assets_dir = s_->sprite_dir;

    // Roles
    s_->edit_roles.clear();
    for (size_t i = 0; i < s_->all_role_ids.size(); ++i) {
        if (s_->role_checked[i]) {
            s_->edit_roles.push_back(s_->all_role_ids[i]);
        }
    }
    config.default_role = s_->edit_roles;

    for (size_t i = 0; i < s_->all_role_ids.size(); ++i) {
        if (!s_->role_checked[i]) continue;

        // Save model
        auto display = s_->available_models[s_->role_model_idx[i]];
        auto bracket_end = display.find("] ");
        if (bracket_end != std::string::npos) {
            std::string provider = display.substr(1, bracket_end - 1);
            std::string model = display.substr(bracket_end + 2);
            RoleConfigManager::SaveModel(s_->all_role_ids[i], provider, model);
        }

    }

    // Security
    config.security.permission_level = s_->perm_level;
    config.security.allow_local_execute = s_->allow_local_exec;

    // TTS
    config.tts.enabled = s_->tts_enabled;
    config.tts.backend = s_->tts_backend;
    config.tts.voice = s_->edge_voice;
    LOG_INFO("[SettingsWindow] TTS config saved: enabled={} backend='{}' voice='{}'",
             config.tts.enabled, config.tts.backend, s_->edge_voice);

    // Per-role TTS voice
    const auto& voice_list = config.tts.voice_list;
    int voice_cnt = static_cast<int>(voice_list.size());
    for (size_t i = 0; i < s_->all_role_ids.size(); ++i) {
        if (!s_->role_checked[i]) continue;
        if (s_->role_voice_idx[i] >= 0 && s_->role_voice_idx[i] < voice_cnt) {
            std::string voice = voice_list[s_->role_voice_idx[i]];
            RoleConfigManager::SaveTtsVoice(s_->all_role_ids[i], voice, config.tts.backend);
            RoleConfigManager::HotSwitchTtsVoice(s_->all_role_ids[i], voice, config.tts.backend);
        }
    }

    // Local Models
    if (!config.llamacpp_models.empty()) {
        config.llamacpp_models[0].port = edit_lm_port_;
        config.llamacpp_models[0].auto_start = edit_lm_auto_start_;
        config.llamacpp_models[0].start_timeout_ms = edit_lm_start_timeout_;
        config.llamacpp_models[0].model_path = edit_lm_model_path_;
    }

    config.SaveToFile();

    // Hot-switch provider/model for checked roles
    for (size_t i = 0; i < s_->all_role_ids.size(); ++i) {
        if (!s_->role_checked[i]) continue;
        auto display = s_->available_models[s_->role_model_idx[i]];
        auto bracket_end = display.find("] ");
        if (bracket_end != std::string::npos) {
            std::string provider = display.substr(1, bracket_end - 1);
            std::string model = display.substr(bracket_end + 2);
            RoleConfigManager::HotSwitch(s_->all_role_ids[i], provider, model);
        }
    }
}

}  // namespace prosophor
