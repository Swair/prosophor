#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/layout_config.h"
#include "prosophor_core/public/config.h"
#include "media_engine/media_engine.h"
#include "common/i18n.h"
#include "virtual_sprite/voice_engine.h"

namespace prosophor {

void ChatWindow::RenderTtsView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("view_tts").c_str(), 30.0f);

    auto Lc = LayoutConfig{};
    auto& config = ProsophorConfig::GetInstance();
    float s = Spacing();
    float iy;
    {
        auto _child = PanelContainer::BeginScroll(f.a, f.btn_h);
        float cx = f.a.x, cw = f.a.w - Lc.section_card_right_margin;

        // ── TTS ──
        {
            Card tts(cx, f.a.y + 8.0f, cw, L.Get("tab_tts").c_str(), s);
            iy = tts.Field(L.Get("tts_enabled").c_str(),
                [&](){ media_engine::ImGuiWidget::Checkbox("##tts_en", &config.tts.enabled); });
            iy = tts.Field(L.Get("tts_backend").c_str(), [&](){
                float cw_half = ((cx + cw) - tts.WidgetX()) * 0.5f;
                auto _w = media_engine::ScopedItemWidth(cw_half);
                const char* be[] = {"edge-tts"}; int bi = 0; media_engine::ImGuiWidget::Combo("##tts_be", &bi, be, 1); });
            iy = tts.Field("Voice", [&](){
                float cw_half = ((cx + cw) - tts.WidgetX()) * 0.5f;
                auto _w = media_engine::ScopedItemWidth(cw_half);
                auto& vl = config.tts.voice_list;
                std::vector<const char*> vc; for (auto& v : vl) vc.push_back(v.c_str());
                int vi = 0; for (int i = 0; i < (int)vl.size(); ++i) { if (config.tts.voice == vl[i]) { vi = i; break; } }
                if (media_engine::ImGuiWidget::Combo("##tts_v", &vi, vc.data(), (int)vc.size()))
                    if (vi >= 0 && vi < (int)vl.size()) config.tts.voice = vl[vi];
            });
            iy += 8.0f * s;
            media_engine::Layout::SetCursorScreenPos(tts.WidgetX(), iy);
            static char test_text[256] = "";
            { float input_w = std::max(60.0f, ((cx + cw) - tts.WidgetX() - 52.0f - 4.0f) * 0.5f);
              auto _w = media_engine::ScopedItemWidth(input_w);
              media_engine::ImGuiWidget::InputText("##tts_test", test_text, sizeof(test_text)); }
            media_engine::Layout::SameLine();
            if (media_engine::ImGuiWidget::Button("Speak", 0, 0))
                VoiceEngine::GetInstance().Speak(test_text, config.tts.backend, config.tts.voice);
            tts.Advance(36.0f * s);
            iy = tts.Y();
        }

        // ── ASR ──
        iy += 30.0f * s;
        {
            Card asr(cx, iy, cw, L.Get("tab_asr").c_str(), s);
            iy = asr.Field("enabled",
                [&](){ media_engine::ImGuiWidget::Checkbox("##asr_en", &config.asr.enabled); });
            iy = asr.Field("push_to_talk",
                [&](){ media_engine::ImGuiWidget::Checkbox("##asr_ptt", &config.asr.push_to_talk); });
            asr.Field("server_url", [&](){
                char buf[512]; std::strncpy(buf, config.asr.server_url.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
                if (media_engine::ImGuiWidget::InputText("##asr_url", buf, sizeof(buf))) config.asr.server_url = buf;
            });
        }
    }

    ActionBar(f.a, f.btn_h,
        [&](){ config.SaveToFile(); },
        [&](){ config = ProsophorConfig::LoadFromFile(ProsophorConfig::DefaultConfigPath()); });
}

} // namespace prosophor
