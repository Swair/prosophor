#include "virtual_sprite/components/panel_kit.h"
#include "virtual_sprite/layout_config.h"
#include "prosophor_core/public/config.h"
#include "media_engine/media_engine.h"
#include "media_engine/media/texture/imgui_widget.h"
#include "common/i18n.h"

namespace prosophor {

// ── ActionBar ──

void ActionBar(const Area& area, float btn_h,
               std::function<void()> on_save,
               std::function<void()> on_cancel)
{
    auto Lc = LayoutConfig{};
    auto& L = I18n::Instance();
    float btn_w = Lc.panel_save_btn_w, btn_d = Lc.panel_btn_gap;
    float by = area.y + area.h - btn_h - 4.0f;
    float bx = area.x + area.w - (on_cancel ? btn_w * 2 + btn_d : btn_w) - Lc.panel_btn_right_gap;
    media_engine::Layout::SetCursorScreenPos(bx, by);
    if (media_engine::ImGuiWidget::Button(L.Get("btn_save").c_str(), btn_w, 0)) {
        if (on_save) on_save();
    }
    if (on_cancel) {
        media_engine::Layout::SameLine();
        media_engine::Layout::Dummy(btn_d, 0);
        media_engine::Layout::SameLine();
        if (media_engine::ImGuiWidget::Button(L.Get("btn_cancel").c_str(), btn_w, 0)) {
            if (on_cancel) on_cancel();
        }
    }
}

// ── SplitPanel ──

SplitPanel::SplitPanel(const Area& content, float left_width,
                       float btn_h, float gap)
{
    auto Lc = LayoutConfig{};
    inner_h = content.h - btn_h - gap - Lc.split_list_text_y;
    divider_x = content.x + left_width;

    left_x = content.x + Lc.split_list_item_gap;
    left_y = content.y + Lc.split_list_item_gap;
    left_w = left_width - Lc.split_list_item_gap * 2;

    right_x = content.x + left_width + Lc.split_left_gap + Lc.split_right_child_pad;
    right_y = content.y + Lc.split_right_child_pad;
    right_w = content.w - left_width - Lc.split_left_gap - Lc.split_right_child_wextra;
}

void SplitPanel::DrawDivider() const {
    auto Lc = LayoutConfig{};
    media_engine::DrawList::RoundRect(divider_x, right_y - Lc.split_right_child_pad + Lc.split_list_text_y,
        Lc.split_divider_w, inner_h - Lc.split_list_text_y * 2, 0,
        media_engine::Colors::CreamBorder);
}

// ── PanelHelper (legacy) ──

PanelHelper::HelperArea PanelHelper::ContentArea(float cont_x, float cont_y, float cont_w, float cont_h) {
    return {cont_x + 12.0f, cont_y + 44.0f, cont_w - 24.0f, cont_h - 56.0f};
}

void PanelHelper::BeginPanel(float cx, float cy, float cw, float ch) {
    media_engine::DrawList::RoundRect(cx, cy, cw, ch, 6.0f, media_engine::Colors::White);
    media_engine::DrawList::RoundRectOutline(cx, cy, cw, ch, 6.0f,
        media_engine::Colors::CreamBorder, 1.0f);
}

void PanelHelper::PanelHeader(const char* title, float cx, float cy, float cw) {
    media_engine::DrawList::Text(cx + 14.0f, cy + 10.0f,
        media_engine::Colors::OrangeDeep, title);
    media_engine::DrawList::RoundRect(cx + 12.0f, cy + 30.0f, cw - 24.0f, 1.0f, 0,
        media_engine::Colors::CreamBorder);
}

media_engine::ScopedChild PanelHelper::BeginScrollContent(
    float cx, float cy, float cw, float ch, float btn_h, float gap)
{
    media_engine::Layout::SetCursorScreenPos(cx + 8.0f, cy + 8.0f);
    return media_engine::ScopedChild(
        "panel_scroll", cw - 16.0f, ch - btn_h - gap - 8.0f,
        0, media_engine::ImGuiWindowFlags_AlwaysVerticalScrollbar);
}

void PanelHelper::SaveCancelBar(float cx, float cy, float cw, float ch,
                                float btn_h,
                                std::function<void()> on_save,
                                std::function<void()> on_cancel) {
    auto Lc = LayoutConfig{};
    auto& L = I18n::Instance();
    float btn_w = Lc.panel_save_btn_w, btn_d = Lc.panel_btn_gap;
    float by = cy + ch - btn_h - 4.0f;
    float bx = cx + cw - (on_cancel ? btn_w * 2 + btn_d : btn_w) - Lc.panel_btn_right_gap;
    media_engine::Layout::SetCursorScreenPos(bx, by);
    if (media_engine::ImGuiWidget::Button(L.Get("btn_save").c_str(), btn_w, 0)) {
        if (on_save) on_save();
    }
    if (on_cancel) {
        media_engine::Layout::SameLine();
        media_engine::Layout::Dummy(btn_d, 0);
        media_engine::Layout::SameLine();
        if (media_engine::ImGuiWidget::Button(L.Get("btn_cancel").c_str(), btn_w, 0)) {
            if (on_cancel) on_cancel();
        }
    }
}

Card::Card(float x, float y, float w, const char* title, float sm,
           const media_engine::Color& bg_color,
           const media_engine::Color& border_color,
           const media_engine::Color& title_color,
           float radius, float fixed_h, bool title_above)
    : x_(x), y_(y), w_(w), fixed_h_(fixed_h), title_(title ? title : "")
    , bg_color_(bg_color), border_color_(border_color), title_color_(title_color)
    , radius_(radius), sm_(sm), title_above_(title_above)
{
    auto Lc = LayoutConfig{};
    content_x_ = x + Lc.card_content_indent;
    widget_x_ = content_x_ + Lc.card_widget_offset;
    // Title above border (auto-shift y_ so title fits within content area)
    if (title_above && title) {
        y_ += 20.0f * sm_;
        next_y_ = y_ + 6.0f * sm_;
    } else {
        next_y_ = y_ + (fixed_h_ <= 0 ? Lc.section_title_gap : fixed_h_);
        if (title)
            media_engine::DrawList::Text(x_ + Lc.label_row_pad, y_ + 10.0f,
                title_color, title_.c_str());
    }
    // Split channels: layer 0 = background (drawn in destructor), layer 1 = content
    media_engine::DrawList::ChannelsSplit(2);
    media_engine::DrawList::ChannelsSetCurrent(1);  // content renders on top layer
}

Card::~Card() {
    auto Lc = LayoutConfig{};
    float card_h = fixed_h_ > 0 ? fixed_h_ : (next_y_ - y_) + 8.0f * sm_;
    float sx = x_ + Lc.section_card_pad;
    float sw = w_ - Lc.section_card_w_extra;
    // Draw background + outline on layer 0 (behind content)
    media_engine::DrawList::ChannelsSetCurrent(0);
    media_engine::DrawList::RoundRect(sx, y_, sw, card_h, radius_, bg_color_);
    media_engine::DrawList::RoundRectOutline(sx, y_, sw, card_h, radius_,
        border_color_, 1.0f);
    // Title text on layer 0 (above card, behind content - safe since no overlap)
    if (title_above_ && !title_.empty())
        media_engine::DrawList::Text(x_ + Lc.label_row_pad, y_ - 20.0f * sm_,
            title_color_, title_.c_str());
    media_engine::DrawList::ChannelsMerge();
}

float Card::Field(const char* label, std::function<void()> widget_fn) {
    next_y_ = PanelHelper::LabelRow(content_x_, next_y_, label, widget_x_,
                                    std::move(widget_fn), sm_);
    return next_y_;
}

FocusCard::FocusCard(float x, float y, float w, float h,
                     bool focused, bool hover, float radius) {
    media_engine::DrawList::RoundRect(x, y, w, h, radius,
        focused ? media_engine::Colors::OrangeLightest
                : hover ? media_engine::Colors::OrangeLightest
                : media_engine::Colors::White);
    if (focused)
        media_engine::DrawList::RoundRectOutline(x, y, w, h, radius,
            media_engine::Colors::OrangeWarm, 1.5f);
}

WhiteCard::WhiteCard(float x, float y, float w, float h,
                     const char* title) {
    auto sm = Spacing();
    if (title) {
        Card card(x, y, w, title, sm,
                  media_engine::Colors::White,
                  media_engine::Colors::CreamBorder,
                  media_engine::Colors::OrangeDeep, 6.0f, h, false);
    } else {
        media_engine::DrawList::RoundRect(x, y, w, h, 6.0f, media_engine::Colors::White);
        media_engine::DrawList::RoundRectOutline(x, y, w, h, 6.0f,
            media_engine::Colors::CreamBorder, 1.0f);
    }
}

float PanelHelper::LabelRow(float cx, float iy, const char* label, float wx,
                            std::function<void()> widget_fn,
                            float spacing_scale) {
    auto Lc = LayoutConfig{};
    media_engine::Layout::SetCursorScreenPos(cx + Lc.label_row_pad, iy);
    media_engine::Text::Colored(media_engine::Colors::Gray55, label);
    media_engine::Layout::SetCursorScreenPos(wx, iy);
    if (widget_fn) widget_fn();
    return iy + Lc.panel_widget_spacing * spacing_scale;
}

float PanelHelper::Spacing(float base, float scale) {
    return base * scale;
}

// ── 通用组件 ──

float CardBox(float x, float y, float w, const char* title) {
    float h = 60.0f;
    media_engine::DrawList::RoundRect(x, y, w, h, 6.0f, media_engine::Colors::White);
    media_engine::DrawList::RoundRectOutline(x, y, w, h, 6.0f,
        media_engine::Colors::CreamBorder, 1.0f);
    media_engine::DrawList::Text(x + 14.0f, y + 10.0f,
        media_engine::Colors::Gray55, title);
    media_engine::DrawList::RoundRect(x + 12.0f, y + 30.0f, w - 24.0f, 1.0f, 0,
        media_engine::Colors::CreamBorder);
    return y + h;
}

float InfoRow(float x, float iy, const char* label, const char* value, float label_w) {
    auto Lc = LayoutConfig{};
    media_engine::DrawList::Text(x, iy, media_engine::Colors::Gray55, label);
    media_engine::DrawList::Text(x + label_w, iy, media_engine::Colors::Gray40, value);
    return iy + Lc.info_row_h * Spacing();
}

float SeparatorLine(float x, float iy, float w, float spacing) {
    media_engine::DrawList::RoundRect(x, iy, w, 1.0f, 0, media_engine::Colors::CreamBorder);
    return iy + spacing;
}

void PlaceholderPage(int cont_x, int cont_y, int cont_w, int cont_h,
                     const char* view_title, const char* section_title,
                     std::initializer_list<const char*> lines)
{
    auto Lc = LayoutConfig{};
    auto& L = I18n::Instance();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, view_title);
    float sm = Spacing();
    float iy = f.a.y + 10.0f * sm;
    if (section_title && section_title[0]) {
        media_engine::DrawList::Text(f.a.x + 14.0f, iy, media_engine::Colors::OrangeDeep, section_title);
        iy = f.a.y + 40.0f * sm;
    }
    for (auto& line : lines) {
        media_engine::DrawList::Text(f.a.x + Lc.content_pad, iy, media_engine::Colors::Gray55, line);
        iy += 22.0f * sm;
    }
    media_engine::DrawList::RoundRect(f.a.x + Lc.content_pad, iy, f.a.w - Lc.content_pad * 2, 1.0f, 0, media_engine::Colors::CreamBorder);
    iy += 16.0f;
    media_engine::DrawList::Text(f.a.x + Lc.content_pad, iy, media_engine::Colors::Gray47, L.Get("panel_coming_soon").c_str());
}

StatCard::StatCard(float x, float y, float w, float h,
                   const char* title, const char* value,
                   const media_engine::Color& accent)
{
    float sm = Spacing();
    media_engine::DrawList::RoundRect(x, y, w, h, 6.0f, media_engine::Colors::White);
    media_engine::DrawList::RoundRectOutline(x, y, w, h, 6.0f,
        media_engine::Colors::CreamBorder, 1.0f);
    media_engine::DrawList::RoundRect(x, y, 3.0f, h, 0, accent);
    media_engine::DrawList::Text(x + 14.0f, y + 10.0f * sm,
        media_engine::Colors::Gray55, title);
    media_engine::DrawList::Text(x + 14.0f, y + 28.0f * sm,
        media_engine::Colors::Gray40, value);
}

} // namespace prosophor
