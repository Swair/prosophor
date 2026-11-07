#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/layout_config.h"
#include "prosophor_core/public/config.h"
#include "media_engine/media_engine.h"
#include "common/i18n.h"
#include "providers/asr_tts_provider_factory.h"
#include "virtual_sprite/voice_engine.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace prosophor {

namespace {

// 后端下拉：候选项来自工厂（本地离线优先），选中项显示规范化后的实际后端。
// 返回 -1 = 本帧未改变选择（避免回退值在未操作时写回配置）。
int DrawBackendCombo(const char* id, const std::vector<std::string>& backends,
                     const std::string& effective) {
    std::vector<const char*> items;
    items.reserve(backends.size());
    for (const auto& b : backends) items.push_back(b.c_str());

    int selected = 0;
    for (int i = 0; i < static_cast<int>(backends.size()); ++i) {
        if (backends[static_cast<size_t>(i)] == effective) {
            selected = i;
            break;
        }
    }
    if (items.empty()) return -1;
    const bool changed = media_engine::ImGuiWidget::Combo(
        id, &selected, items.data(), static_cast<int>(items.size()));
    return changed ? selected : -1;
}

}  // namespace

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
                const auto backends = TtsProviderFactory::AvailableBackends();
                const std::string effective =
                    TtsProviderFactory::EffectiveBackend(config.tts);
                const int selected = DrawBackendCombo("##tts_be", backends, effective);
                if (selected >= 0)
                    config.tts.backend = backends[static_cast<size_t>(selected)];
            });

            if (TtsProviderFactory::EffectiveBackend(config.tts) == "edge-tts") {
                iy = tts.Field("Voice", [&](){
                    float cw_half = ((cx + cw) - tts.WidgetX()) * 0.5f;
                    auto _w = media_engine::ScopedItemWidth(cw_half);
                    auto& vl = config.tts.voice_list;
                    std::vector<const char*> vc; for (auto& v : vl) vc.push_back(v.c_str());
                    int vi = 0; for (int i = 0; i < (int)vl.size(); ++i) { if (config.tts.voice == vl[i]) { vi = i; break; } }
                    if (!vc.empty() && media_engine::ImGuiWidget::Combo("##tts_v", &vi, vc.data(), (int)vc.size()))
                        if (vi >= 0 && vi < (int)vl.size()) config.tts.voice = vl[vi];
                });
            } else {
                iy = tts.Field(L.Get("tts_model_dir").c_str(), [&](){
                    float cw_half = ((cx + cw) - tts.WidgetX()) * 0.5f;
                    auto _w = media_engine::ScopedItemWidth(cw_half);
                    char buf[512];
                    std::strncpy(buf, config.tts.sherpa_model_dir.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = 0;
                    if (media_engine::ImGuiWidget::InputText("##tts_model_dir", buf, sizeof(buf)))
                        config.tts.sherpa_model_dir = buf;
                });
                iy = tts.Field(L.Get("tts_speed").c_str(), [&](){
                    float cw_half = ((cx + cw) - tts.WidgetX()) * 0.5f;
                    auto _w = media_engine::ScopedItemWidth(cw_half);
                    double speed = config.tts.sherpa_speed;
                    if (media_engine::ImGuiWidget::SliderFloat("##tts_speed", &speed, 0.5f, 2.0f, "%.2f"))
                        config.tts.sherpa_speed = static_cast<float>(speed);
                });
            }

            media_engine::Text::Colored(media_engine::Colors::Gray55,
                                        L.Get("tts_restart_hint").c_str());
            iy += 24.0f * s;
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
            iy = asr.Field(L.Get("tts_enabled").c_str(),
                [&](){ media_engine::ImGuiWidget::Checkbox("##asr_en", &config.asr.enabled); });
            iy = asr.Field(L.Get("asr_push_to_talk").c_str(),
                [&](){ media_engine::ImGuiWidget::Checkbox("##asr_ptt", &config.asr.push_to_talk); });
            iy = asr.Field(L.Get("asr_backend").c_str(), [&](){
                float cw_half = ((cx + cw) - asr.WidgetX()) * 0.5f;
                auto _w = media_engine::ScopedItemWidth(cw_half);
                const auto backends = AsrProviderFactory::AvailableBackends();
                const std::string effective =
                    AsrProviderFactory::EffectiveBackend(config.asr);
                const int selected = DrawBackendCombo("##asr_be", backends, effective);
                if (selected >= 0)
                    config.asr.backend = backends[static_cast<size_t>(selected)];
            });

            const std::string effective_asr =
                AsrProviderFactory::EffectiveBackend(config.asr);
            if (effective_asr == "sherpa-onnx") {
                iy = asr.Field(L.Get("asr_model_dir").c_str(), [&](){
                    float cw_half = ((cx + cw) - asr.WidgetX()) * 0.5f;
                    auto _w = media_engine::ScopedItemWidth(cw_half);
                    char buf[512];
                    std::strncpy(buf, config.asr.sherpa_model_dir.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = 0;
                    if (media_engine::ImGuiWidget::InputText("##asr_model_dir", buf, sizeof(buf)))
                        config.asr.sherpa_model_dir = buf;
                });
                iy = asr.Field(L.Get("asr_language").c_str(), [&](){
                    float cw_half = ((cx + cw) - asr.WidgetX()) * 0.5f;
                    auto _w = media_engine::ScopedItemWidth(cw_half);
                    char buf[64];
                    std::strncpy(buf, config.asr.language.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = 0;
                    if (media_engine::ImGuiWidget::InputText("##asr_lang", buf, sizeof(buf)))
                        config.asr.language = buf;
                });
                iy = asr.Field(L.Get("asr_threads").c_str(), [&](){
                    float cw_half = ((cx + cw) - asr.WidgetX()) * 0.5f;
                    auto _w = media_engine::ScopedItemWidth(cw_half);
                    media_engine::ImGuiWidget::InputInt("##asr_threads", &config.asr.sherpa_num_threads);
                });
                iy = asr.Field(L.Get("asr_prefer_fp32").c_str(), [&](){
                    media_engine::ImGuiWidget::Checkbox("##asr_fp32", &config.asr.sherpa_prefer_fp32);
                });
            }
            media_engine::Text::Colored(media_engine::Colors::Gray55,
                                        L.Get("asr_restart_hint").c_str());
        }
    }

    ActionBar(f.a, f.btn_h,
        [&](){ config.SaveToFile(); },
        [&](){ config = ProsophorConfig::LoadFromFile(ProsophorConfig::DefaultConfigPath()); });
}

} // namespace prosophor
