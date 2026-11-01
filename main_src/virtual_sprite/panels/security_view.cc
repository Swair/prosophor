#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "media_engine/media_engine.h"
#include "common/i18n.h"
#include "prosophor_core/public/config.h"
#include "prosophor_core/public/security.h"

namespace prosophor {

void ChatWindow::RenderSecurityView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    float sm = Spacing();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("view_security").c_str());

    auto& cfg = ProsophorConfig::GetInstance();
    auto& perm = PermissionManager::GetInstance();

    float cx = f.a.x + 8.0f, cy = f.a.y + 8.0f, cw = f.a.w - 24.0f;

    media_engine::DrawList::RoundRect(cx, cy, cw, 160.0f * sm, 6.0f,
        media_engine::Colors::Beige);
    media_engine::DrawList::RoundRectOutline(cx, cy, cw, 160.0f * sm,
        6.0f, media_engine::Colors::Gray63, 1.0f);

    float iy = cy + 12.0f * sm;
    media_engine::DrawList::Text(cx + 12.0f, iy, media_engine::Colors::OrangeDeep,
        L.Get("perm_title").c_str());
    iy += 26.0f * sm;

    // ── Permission mode selector ──
    struct ModeOption {
        std::string key;
        const char* label;
        const char* desc;
    };
    ModeOption modes[] = {
        {"auto",      L.Get("perm_auto").c_str(),      L.Get("perm_auto_desc").c_str()},
        {"bypass",    L.Get("perm_full").c_str(),       L.Get("perm_full_desc").c_str()},
        {"ask",       L.Get("perm_ask").c_str(),        L.Get("perm_ask_desc").c_str()},
    };

    for (auto& mo : modes) {
        bool selected = (cfg.security.permission_level == mo.key);
        float bx = cx + 12.0f, by = iy;
        media_engine::Layout::SetCursorScreenPos(bx, by);
        if (media_engine::ImGuiWidget::Button(mo.label, 120.0f * sm, 24.0f * sm)) {
            cfg.security.permission_level = mo.key;
            perm.SetMode(mo.key);
            cfg.SaveToFile();
        }
        if (selected) {
            float iw, ih; media_engine::ImGuiWidget::GetItemRectSize(&iw, &ih);
            media_engine::DrawList::RoundRectOutline(bx, by, iw, ih, 4.0f,
                media_engine::Colors::OrangeWarm, 2.0f);
        }
        media_engine::DrawList::Text(bx + 126.0f * sm, by + 3.0f, media_engine::Colors::Gray55, mo.desc);
        iy += 28.0f * sm;
    }
}

} // namespace prosophor
