#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "virtual_sprite/components/panel_kit.h"
#include "common/i18n.h"
#include "prosophor_core/public/mcp.h"

namespace prosophor {

void ChatWindow::RenderMcpView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("view_mcp").c_str());
    float sm = Spacing();

    auto& mcp = McpClient::GetInstance();
    auto servers = mcp.GetConfiguredServers();

    {
        auto _child = PanelContainer::BeginScroll(f.a, 0, 4.0f);
        float cx = f.a.x + 8.0f, cw = f.a.w - 24.0f;
        float y = f.a.y + 8.0f;

        if (servers.empty()) {
            Card(cx, y, cw, L.Get("panel_no_data").c_str(), sm);
            return;
        }

        for (auto& sv : servers) {
            bool connected = mcp.IsConnected(sv.name);
            std::string title = (connected ? "\xf0\x9f\x94\x97 " : "\xe2\x9b\x94 ") + sv.name;  // 🔗 / ⛔
            {
                Card card(cx, y, cw, title.c_str(), sm);
                y = card.Y();
                std::string info = sv.type + " | " + (sv.command.empty() ? sv.url : sv.command);
                media_engine::DrawList::Text(cx + 38.0f, y,
                    media_engine::Colors::Gray55, info.c_str());
                card.Advance(22.0f * sm);
                y = card.Y();
            }
            y += 8.0f * sm;
        }
    }
}

} // namespace prosophor
