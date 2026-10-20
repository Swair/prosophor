#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/model_card.h"
#include "virtual_sprite/panels/provider_entry_card.h"
#include "virtual_sprite/components/item_list.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/layout_config.h"
#include "config/config.h"
#include "media_engine/media_engine.h"
#include "common/i18n.h"
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace prosophor {

namespace {

static std::string s_sel;
static bool s_scan = true;
static std::vector<std::string> s_order;

void InitOrder(const std::unordered_map<std::string, LlmProviderConfig>& providers) {
    s_order.clear();
    const std::vector<std::string> preferred = {
        "anthropic", "openai", "ollama", "llamacpp"
    };
    for (auto& name : preferred)
        if (providers.find(name) != providers.end()) s_order.push_back(name);
    for (auto& [name, _] : providers)
        if (std::find(s_order.begin(), s_order.end(), name) == s_order.end())
            s_order.push_back(name);
    if (s_sel.empty() || providers.find(s_sel) == providers.end())
        s_sel = s_order.empty() ? "" : s_order[0];
}

} // anonymous namespace

void ChatWindow::RenderProvidersView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto Lc = LayoutConfig{};
    auto& L = I18n::Instance();
    float sm = Spacing();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("view_providers").c_str(), 30.0f);

    auto& config = ProsophorConfig::GetInstance();

    if (s_scan) { InitOrder(config.llm_providers); s_scan = false; }

    float gap = 12.0f;
    float left_w = Lc.panel_left_list_w;

    auto sv = SplitPanel(f.a, left_w, f.btn_h, gap);

    // ── Left inner: provider list ──
    {
        media_engine::Layout::SetCursorScreenPos(sv.left_x, sv.left_y);
        auto _l = media_engine::ScopedChild("pv_list", sv.left_w, sv.inner_h, 0,
            media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        {
            ItemList list(sv.left_x, sv.left_y, sv.left_w, sm);
            for (auto& name : s_order) {
                if (list.Item(("ps_" + name).c_str(), name.c_str(), name == s_sel))
                    s_sel = name;
            }
        }
    }

    sv.DrawDivider();

    // ── Right inner: provider config ──
    {
        media_engine::Layout::SetCursorScreenPos(sv.right_x, sv.right_y);
        auto _r = media_engine::ScopedChild("pv_cfg", sv.right_w, sv.inner_h,
            0, media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);

        auto it = config.llm_providers.find(s_sel);
        if (it == config.llm_providers.end()) {
            media_engine::Text::Colored(media_engine::Colors::Gray55, s_sel.empty()
                ? L.Get("panel_no_data").c_str()
                : (L.Get("panel_no_data") + " (" + s_sel + ")").c_str());
            return;
        }
        auto& prov = it->second;

        if (prov.entries.empty()) {
            media_engine::Text::Colored(media_engine::Colors::Gray55, "(no entries)");
            media_engine::Layout::Dummy(0, 4.0f * sm);
        }
        for (size_t ei = 0; ei < prov.entries.size(); ++ei) {
            media_engine::Layout::Dummy(0, 6.0f * sm);
            ProviderEntryCard card(prov.entries[ei], ei, sv.right_w - 8.0f, sm);
            card.Render();
        }

        media_engine::Layout::Dummy(0, 6.0f * sm);
        float add_entry_y;
        media_engine::Layout::GetCursorScreenPos(nullptr, &add_entry_y);
        media_engine::Layout::SetCursorScreenPos(sv.right_x + sv.right_w - 140.0f - 8.0f, add_entry_y);
        if (media_engine::ImGuiWidget::Button(("+ Add Entry##" + s_sel).c_str(), 140.0f, 0)) {
            LlmProviderEntryConfig ne;
            ne.timeout = 30;
            prov.entries.push_back(std::move(ne));
        }
    }

    ActionBar(f.a, f.btn_h,
        [&](){ config.SaveToFile(); },
        [&](){ config = ProsophorConfig::LoadFromFile(ProsophorConfig::DefaultConfigPath());
               s_scan = true; });
}

} // namespace prosophor
