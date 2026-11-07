#include <cstring>
#include "virtual_sprite/panels/provider_entry_card.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/panels/model_card.h"
#include "virtual_sprite/layout_config.h"

namespace prosophor {

std::unordered_map<size_t, bool> ProviderEntryCard::s_models_open;

ProviderEntryCard::ProviderEntryCard(LlmProviderEntryConfig& entry,
                                     size_t entry_idx,
                                     float width,
                                     float sm)
    : entry_(entry)
    , entry_idx_(entry_idx)
    , width_(width)
    , sm_(sm) {}

std::string ProviderEntryCard::Id(const char* suf) const {
    return "##pv_" + std::to_string(entry_idx_) + "_" + suf;
}

void ProviderEntryCard::RenderField(const char* label,
                                    float entry_fy, float entry_cx,
                                    float widget_w, float entry_lh,
                                    std::function<void(float)> widget_fn,
                                    float& next_fy)
{
    media_engine::Layout::SetCursorScreenPos(entry_cx + 8.0f, entry_fy);
    media_engine::Text::Colored(media_engine::Colors::Gray55, label);
    media_engine::Layout::SetCursorScreenPos(entry_cx + 120.0f, entry_fy);
    auto _fw = media_engine::ScopedItemWidth(widget_w);
    if (widget_fn) widget_fn(widget_w);
    next_fy = entry_fy + entry_lh;
}

void ProviderEntryCard::Render() {
    BorderedContainer _entry(
        ("##pv_entry_" + std::to_string(entry_idx_)).c_str(),
        width_);

    float cw = media_engine::Layout::GetContentRegionAvailWidth();
    float widget_w = (cw - 120.0f) * 2.0f / 3.0f;

    float entryFY;
    media_engine::Layout::GetCursorScreenPos(nullptr, &entryFY);
    float entryCX;
    media_engine::Layout::GetCursorScreenPos(&entryCX, nullptr);
    auto Lc = LayoutConfig{};
    float entryLH = Lc.panel_widget_spacing * sm_;

    char buf[1024];
    RenderField("api_key", entryFY, entryCX, widget_w, entryLH,
        [&](float) {
            std::strncpy(buf, entry_.api_key.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            if (media_engine::ImGuiWidget::InputText(Id("ak").c_str(), buf, sizeof(buf)))
                entry_.api_key = buf;
        }, entryFY);
    RenderField("base_url", entryFY, entryCX, widget_w, entryLH,
        [&](float) {
            std::strncpy(buf, entry_.base_url.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            if (media_engine::ImGuiWidget::InputText(Id("url").c_str(), buf, sizeof(buf)))
                entry_.base_url = buf;
        }, entryFY);
    RenderField("timeout", entryFY, entryCX, widget_w, entryLH,
        [&](float) {
            media_engine::ImGuiWidget::InputInt(Id("to").c_str(), &entry_.timeout);
        }, entryFY);
    RenderField("thinking", entryFY, entryCX, widget_w, entryLH,
        [&](float) {
            media_engine::ImGuiWidget::Checkbox(Id("thk").c_str(), &entry_.thinking);
        }, entryFY);

    media_engine::Layout::Dummy(0, 4.0f * sm_);

    // ── Models section ──
    float mCW = cw - 8.0f;
    if (s_models_open.find(entry_idx_) == s_models_open.end())
        s_models_open[entry_idx_] = true;
    bool& m_open = s_models_open[entry_idx_];

    float sy;
    media_engine::Layout::GetCursorScreenPos(nullptr, &sy);
    float ty = sy + 2.0f * sm_;
    media_engine::Layout::SetCursorScreenPos(entryCX + 8.0f, ty);
    std::string arrow_str = m_open ? " -" : " +";
    media_engine::Text::Colored(media_engine::Colors::OrangeDeep,
        ("models" + arrow_str).c_str());

    media_engine::Layout::SetCursorScreenPos(entryCX + 8.0f, sy);
    if (media_engine::ImGuiWidget::InvisibleButton(
            ("##mdl_click_" + std::to_string(entry_idx_)).c_str(),
            cw - 8.0f, 20.0f * sm_))
        m_open = !m_open;

    if (m_open) {
        float model_x0 = entryCX + 8.0f;
        for (auto& [mk, mv] : entry_.models) {
            float cy;
            media_engine::Layout::GetCursorScreenPos(nullptr, &cy);
            media_engine::Layout::SetCursorScreenPos(model_x0, cy);
            ModelCard card(mv, entry_idx_, mk, mCW, sm_);
            card.Render();
        }

        float btn_y;
        media_engine::Layout::GetCursorScreenPos(nullptr, &btn_y);
        media_engine::Layout::SetCursorScreenPos(entryCX + cw - 140.0f - 8.0f, btn_y);
        if (media_engine::ImGuiWidget::Button(
                ("+ Add Model##" + std::to_string(entry_idx_)).c_str(), 140.0f, 0)) {
            int n = (int)entry_.models.size() + 1;
            std::string new_key = "new-model-" + std::to_string(n);
            while (entry_.models.count(new_key)) { ++n; new_key = "new-model-" + std::to_string(n); }
            ModelConfig mc;
            mc.name = new_key;
            mc.model = new_key;
            entry_.models[new_key] = mc;
        }
    }
}

} // namespace prosophor
