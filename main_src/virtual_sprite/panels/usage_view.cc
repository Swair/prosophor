// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/chat_window.h"
#include "virtual_sprite/panels/panel_helpers.h"
#include "media_engine/media_engine.h"
#include "common/i18n.h"
#include "prosophor_core/public/token_tracker.h"

#include <sstream>
#include <iomanip>
#include <chrono>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prosophor {
namespace {

constexpr float kStepBtnW = 20.0f;  // +/- stepper button width
constexpr float kDateTextW = 95.0f; // date text display width

// Format integer with comma separators (e.g. 1234 -> "1,234")
std::string CommaSep(int n) {
    auto s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<size_t>(i), ",");
    return s;
}

// Format cost (auto-scale: show $0.xxxx for small, $x.xx for normal)
std::string CostStr(double cost) {
    std::ostringstream oss;
    oss.precision(cost < 0.01 ? 4 : 2);
    oss << "$" << std::fixed << cost;
    return oss.str();
}

// Get date string for N days ago (YYYY-MM-DD)
std::string DateNDaysAgo(int n) {
    auto tp = std::chrono::system_clock::now() - std::chrono::hours(24 * n);
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

// Short date MM/DD for chart X-axis labels
std::string ShortDate(const std::string& yyyy_mm_dd) {
    if (yyyy_mm_dd.size() < 10) return yyyy_mm_dd;
    return yyyy_mm_dd.substr(5);  // "06-25"
}

// Catmull-Rom interpolation: smooth curve through control points p0..p3
// t in [0,1] interpolates between p1 and p2
float CatmullRom(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

// Draw a small stepper button [<] or [>], returns true if clicked
bool StepButton(float x, float y, float w, float h, const char* label) {
    media_engine::Layout::SetCursorPos(static_cast<int>(x), static_cast<int>(y));
    return media_engine::ImGuiWidget::Button(label, w, h);
}

// ── Calendar helpers ──

struct YMD { int year, month, day; };

int DaysInMonth(int year, int month) {
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12: return 31;
        case 4: case 6: case 9: case 11: return 30;
        case 2: return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
        default: return 0;
    }
}

// Zeller's formula: day of week (0=Sun, 1=Mon, ..., 6=Sat)
int DayOfWeek(int year, int month, int day) {
    if (month < 3) { month += 12; year--; }
    return (day + (13 * (month + 1)) / 5 + year + year / 4 - year / 100 + year / 400) % 7;
}

YMD DaysAgoToYMD(int n) {
    auto tp = std::chrono::system_clock::now() - std::chrono::hours(24 * n);
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

int YMDToDaysAgo(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    auto now = std::chrono::system_clock::now();
    auto diff = now - tp;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24);
}

} // anonymous namespace

void ChatWindow::RenderUsageView(int cont_x, int cont_y, int cont_w, int cont_h) {
    auto& L = I18n::Instance();
    auto& tracker = TokenTracker::GetInstance();
    auto total = tracker.GetTotalStats();
    auto daily = tracker.GetDailyHistory();
    float sm = Spacing();
    PanelContainer f(cont_x, cont_y, cont_w, cont_h, L.Get("view_usage").c_str());

    // ── Stat cards row ──
    float cx = f.a.x + 8.0f, cy = f.a.y + 8.0f, gap = 12.0f * sm, ch = 60.0f * sm;
    float cw = (f.a.w - 28.0f - gap * 2) / 3.0f;
    StatCard(cx, cy, cw, ch, L.Get("usage_total_tokens").c_str(), CommaSep(total.total_tokens).c_str(), media_engine::Colors::Orange);
    StatCard(cx + cw + gap, cy, cw, ch, L.Get("usage_input_tokens").c_str(), CommaSep(total.prompt_tokens).c_str(), media_engine::Colors::BlueMid);
    StatCard(cx + (cw + gap) * 2, cy, cw, ch, L.Get("usage_output_tokens").c_str(), CommaSep(total.completion_tokens).c_str(), media_engine::Colors::Purple);
    cy += ch + gap;
    StatCard(cx, cy, cw, ch, L.Get("usage_est_cost").c_str(), CostStr(total.cost_usd).c_str(), media_engine::Colors::GreenSuccess);

    // ── Table + Chart area ──
    cy += ch + 20.0f * sm;
    float remain_h = f.a.y + f.a.h - cy - 8.0f;
    float table_h = std::min(200.0f * sm, remain_h * 0.45f);
    float chart_h = remain_h - table_h - 12.0f * sm;
    float tw = f.a.w - 24.0f;

    // ── Table: per-model stats ──
    WhiteCard(cx, cy, tw, table_h);
    float ty = cy + 10.0f * sm;
    float c1 = cx + 14.0f;
    float c2 = cx + tw * 0.38f;
    float c3 = cx + tw * 0.55f;
    float c4 = cx + tw * 0.75f;
    media_engine::DrawList::Text(c1, ty, media_engine::Colors::OrangeDeep, "Model");
    media_engine::DrawList::Text(c2, ty, media_engine::Colors::Gray55, "Input");
    media_engine::DrawList::Text(c3, ty, media_engine::Colors::Gray55, "Output");
    media_engine::DrawList::Text(c4, ty, media_engine::Colors::Gray55, "Cost");
    ty += 22.0f * sm;
    static std::string sel_model;  // empty = All
    auto all_stats = tracker.GetAllStats();
    for (auto& [model, stats] : all_stats) {
        bool active = (model == sel_model);
        auto mc = active ? media_engine::Colors::OrangeDeep : media_engine::Colors::Gray40;
        media_engine::Layout::SetCursorScreenPos(c1, ty);
        if (media_engine::ImGuiWidget::InvisibleButton(("ms_" + model).c_str(), c4 - c1, 18.0f * sm)) {
            sel_model = active ? "" : model;
        }
        if (active || media_engine::Mouse::IsItemHovered()) {
            media_engine::DrawList::RoundRect(c1 - 2.0f, ty - 1.0f, c4 - c1 + 4.0f, 20.0f * sm, 3.0f,
                active ? media_engine::Colors::OrangeLight : media_engine::Colors::Gray90);
        }
        media_engine::DrawList::Text(c1, ty, mc, model.c_str());
        media_engine::DrawList::Text(c2, ty, media_engine::Colors::Gray40, CommaSep(stats.prompt_tokens).c_str());
        media_engine::DrawList::Text(c3, ty, media_engine::Colors::Gray40, CommaSep(stats.completion_tokens).c_str());
        media_engine::DrawList::Text(c4, ty, media_engine::Colors::Gray40, CostStr(stats.cost_usd).c_str());
        ty += 20.0f * sm;
    }

    // ── Chart: daily token usage with date range selection ──
    float chart_y = cy + table_h + 12.0f * sm;
    float chart_x = cx;
    float chart_w = tw;
    float chart_area_h = chart_h;

    // Chart background
    media_engine::DrawList::RoundRect(chart_x, chart_y, chart_w, chart_area_h, 6.0f,
        media_engine::Colors::White);

    // ── Date range state (always visible) ──
    static int start_days_ago = 13;  // start = N days ago
    static int end_days_ago = 0;     // end = N days ago (0=today)
    static int active_preset = 14;   // 7, 14, 30, 0=All

    // ── Title ──
    char chart_title[64];
    // populated after date range is determined below

    // ── Unified button styling ──
    auto BtnStyle = [](bool selected) {
        if (selected)
            return media_engine::ScopedColors(
                media_engine::Color::Slot::Button, media_engine::Colors::Orange)
                .Then(media_engine::Color::Slot::ButtonHovered, media_engine::Colors::OrangeWarm)
                .Then(media_engine::Color::Slot::ButtonActive, media_engine::Colors::OrangeDeep)
                .Then(media_engine::Color::Slot::Text, media_engine::Colors::White);
        return media_engine::ScopedColors(
            media_engine::Color::Slot::Button, media_engine::Colors::Gray95)
            .Then(media_engine::Color::Slot::ButtonHovered, media_engine::Colors::OrangeLight)
            .Then(media_engine::Color::Slot::ButtonActive, media_engine::Colors::OrangePale)
            .Then(media_engine::Color::Slot::Text, media_engine::Colors::Gray40);
    };

    // ── Preset buttons + date steppers (same row as title) ──
    float row_y = chart_y + 6.0f;
    float range_btn_h = 20.0f * sm;
    float label_w = 36.0f * sm;
    float dg_one_w = label_w + kStepBtnW + 3.0f + kDateTextW + 5.0f + kStepBtnW;
    float dg_total_w = dg_one_w + 12.0f * sm + dg_one_w;

    // Always shift preset buttons left for date groups on the right
    float date_groups_right = chart_x + chart_w - 8.0f;
    float date_groups_left = date_groups_right - dg_total_w;
    float preset_right = date_groups_left - 8.0f * sm;

    // Clamp date bounds
    if (start_days_ago < end_days_ago) start_days_ago = end_days_ago;
    if (end_days_ago < 0) end_days_ago = 0;

    // Preset buttons: 7d, 14d, 30d, All (no Cstm)
    struct RangeOption { int days; const char* label; };
    RangeOption ranges[] = {{7, "7d"}, {14, "14d"}, {30, "30d"}, {0, "All"}};
    constexpr int kNumRanges = 4;
    float range_btn_x = preset_right;
    for (int ri = kNumRanges - 1; ri >= 0; --ri) {
        auto& opt = ranges[ri];
        float btn_w = 38.0f * sm;
        range_btn_x -= btn_w + 4.0f * sm;
        bool selected = (active_preset == opt.days);

        auto _s = BtnStyle(selected);
        media_engine::Layout::SetCursorPos(static_cast<int>(range_btn_x), static_cast<int>(row_y));
        if (media_engine::ImGuiWidget::Button(opt.label, btn_w, range_btn_h)) {
            active_preset = opt.days;
            if (opt.days > 0) {
                start_days_ago = opt.days - 1;
                end_days_ago = 0;
            } else {
                // All: use max available data
                start_days_ago = std::max(static_cast<int>(daily.size()) - 1, 0);
                end_days_ago = 0;
            }
        }
    }

    // ── Date stepper (always visible, to the right of preset buttons) ──
    // Calendar popup state
    static int cal_target = 0;   // 0=start, 1=end
    static int cal_year = 0, cal_month = 0;
    {
        float stepper_h = 20.0f * sm;
        float gap_between = 12.0f * sm;

        auto DrawDateGroup = [&](float gx, const char* label,
                                 const media_engine::Color& label_color,
                                 int& days_ago, int min_days, int tid) {
            media_engine::DrawList::Text(gx, row_y + (range_btn_h - 13.0f) * 0.5f, label_color, label);
            float sx = gx + label_w;
            float btn_h = stepper_h - 7.0f;
            float btn_off = (stepper_h - btn_h) * 0.5f - 1.0f;
            { char b[16]; std::snprintf(b, sizeof(b), "<##dg%d", tid); auto _s = BtnStyle(false); if (StepButton(sx, row_y + btn_off, kStepBtnW, btn_h, b)) { days_ago++; active_preset = 0; } }
            sx += kStepBtnW + 3.0f;
            auto date = DateNDaysAgo(days_ago);
            media_engine::DrawList::RoundRect(sx, row_y - 1.0f, kDateTextW, stepper_h, 4.0f, media_engine::Colors::White);
            media_engine::DrawList::Text(sx + 6.0f, row_y + (range_btn_h - 13.0f) * 0.5f, media_engine::Colors::Gray35, date.c_str());
            media_engine::Layout::SetCursorPos(static_cast<int>(sx), static_cast<int>(row_y));
            char ci[16]; std::snprintf(ci, sizeof(ci), "##cal%d", tid);
            if (media_engine::ImGuiWidget::InvisibleButton(ci, kDateTextW, stepper_h)) {
                cal_target = tid; auto ymd = DaysAgoToYMD(days_ago);
                cal_year = ymd.year; cal_month = ymd.month;
                media_engine::Popup::Open("cal_picker");
            }
            sx += kDateTextW + 5.0f;
            { char b2[16]; std::snprintf(b2, sizeof(b2), ">##dg%d", tid); auto _s = BtnStyle(false); if (StepButton(sx, row_y + btn_off, kStepBtnW, btn_h, b2)) if (days_ago > min_days) { days_ago--; active_preset = 0; } }
        };

        DrawDateGroup(date_groups_left, L.Get("usage_start").c_str(),
                      media_engine::Colors::OrangeDeep, start_days_ago, end_days_ago, 0);
        float end_x = date_groups_left + dg_one_w + gap_between;
        DrawDateGroup(end_x, L.Get("usage_end").c_str(),
                      media_engine::Colors::BlueMid, end_days_ago, 0, 1);
    }

    // ── Calendar popup ──
    if (cal_year > 0 && cal_month > 0) {
        if (auto _cal = media_engine::ScopedPopupMenu("cal_picker")) {
            float btn_sz = 22.0f;
            {
                auto _s = BtnStyle(false);
                if (media_engine::ImGuiWidget::Button("<##calp", btn_sz, btn_sz))
                    if (--cal_month < 1) { cal_month = 12; cal_year--; }
            }
            media_engine::Layout::SameLine();
            char mon_str[32];
            std::snprintf(mon_str, sizeof(mon_str), "  %d-%02d  ", cal_year, cal_month);
            media_engine::Text::Raw(mon_str);
            media_engine::Layout::SameLine();
            {
                auto _s = BtnStyle(false);
                if (media_engine::ImGuiWidget::Button(">##calp", btn_sz, btn_sz))
                    if (++cal_month > 12) { cal_month = 1; cal_year++; }
            }

            const char* wdays = "Su Mo Tu We Th Fr Sa";
            media_engine::Text::Raw(wdays);

            int dim = DaysInMonth(cal_year, cal_month);
            int dow = DayOfWeek(cal_year, cal_month, 1);
            YMD today = DaysAgoToYMD(0);

            for (int d = 1; d <= dim; ++d) {
                if ((dow + d - 1) % 7 != 0)
                    media_engine::Layout::SameLine();
                bool is_today = (d == today.day && cal_month == today.month && cal_year == today.year);
                char dbuf[4]; std::snprintf(dbuf, sizeof(dbuf), "%d", d);
                auto _s = BtnStyle(is_today);
                if (media_engine::ImGuiWidget::Button(dbuf, btn_sz, btn_sz)) {
                    int na = YMDToDaysAgo(cal_year, cal_month, d);
                    if (cal_target == 0) {
                        start_days_ago = na;
                        if (start_days_ago < end_days_ago) start_days_ago = end_days_ago;
                    } else {
                        end_days_ago = na;
                        if (end_days_ago < 0) end_days_ago = 0;
                        if (end_days_ago > start_days_ago) end_days_ago = start_days_ago;
                    }
                    active_preset = 0;
                    media_engine::Popup::CloseCurrentPopup();
                }
            }
        }
    }

    // ── Determine date range ──
    int num_days = start_days_ago - end_days_ago + 1;
    int start_offset = start_days_ago;

    // ── Title ──
    auto sd = DateNDaysAgo(start_days_ago);
    auto ed = DateNDaysAgo(end_days_ago);
    std::snprintf(chart_title, sizeof(chart_title), "Token Usage (%s ~ %s)",
                  ShortDate(sd).c_str(), ShortDate(ed).c_str());

    // ── Build sorted daily data ──
    std::vector<int> day_values(static_cast<size_t>(num_days), 0);
    int max_val = 0;
    auto daily_model = tracker.GetDailyModelHistory();
    for (int i = 0; i < num_days; ++i) {
        auto date = DateNDaysAgo(start_offset - i);  // oldest first
        if (sel_model.empty()) {
            // All models
            auto it = daily.find(date);
            if (it != daily.end()) {
                day_values[static_cast<size_t>(i)] = it->second.total_tokens;
                max_val = std::max(max_val, it->second.total_tokens);
            }
        } else {
            // Specific model
            auto dit = daily_model.find(date);
            if (dit != daily_model.end()) {
                auto mit = dit->second.find(sel_model);
                if (mit != dit->second.end()) {
                    day_values[static_cast<size_t>(i)] = mit->second.total_tokens;
                    max_val = std::max(max_val, mit->second.total_tokens);
                }
            }
        }
    }

    // ── Chart layout ──
    float pad_l = 50.0f, pad_r = 12.0f;
    float pad_t = 6.0f + 20.0f * sm + 6.0f;  // row_y(6) + btn_h + gap
    float pad_b = 28.0f;
    float plot_x = chart_x + pad_l;
    float plot_y = chart_y + pad_t;
    float plot_w = chart_w - pad_l - pad_r;
    float plot_h = chart_area_h - pad_t - pad_b;

    if (plot_w > 0 && plot_h > 0 && max_val > 0 && num_days >= 1) {
        const float base_y = plot_y + plot_h;

        // ── Y-axis grid lines and labels ──
        constexpr int kGridLines = 4;
        for (int gi = 0; gi <= kGridLines; ++gi) {
            float y = plot_y + plot_h * (1.0f - static_cast<float>(gi) / kGridLines);
            int val = max_val * gi / kGridLines;
            media_engine::DrawList::Line(plot_x, y, plot_x + plot_w, y,
                media_engine::Colors::Gray90, 1.0f);
            media_engine::DrawList::Text(plot_x - 4.0f, y - 6.0f,
                media_engine::Colors::Gray55, CommaSep(val).c_str());
        }

        if (num_days == 1) {
            // Single point: draw a dot
            float x = plot_x + plot_w * 0.5f;
            float y = plot_y;
            media_engine::DrawList::CircleFilled(x, y, 4.0f, media_engine::Colors::Orange);
        } else {
            // ── Generate smooth curve points via Catmull-Rom ──
            float step = plot_w / (num_days - 1);
            constexpr int kSubDiv = 10;  // subdivisions per segment

            // Build point positions: map day index -> plot position
            auto XPos = [&](int idx) { return plot_x + idx * step; };
            auto YPos = [&](int val) {
                return base_y - plot_h * (static_cast<float>(val) / max_val);
            };

            // Collect interpolated curve points
            struct Point { float x, y; };
            std::vector<Point> curve_pts;
            curve_pts.reserve(static_cast<size_t>((num_days - 1) * kSubDiv + 1));

            for (int i = 0; i < num_days - 1; ++i) {
                // Interpolate in data-value space, then convert to pixel Y.
                // Catmull-Rom in pixel space can overshoot below baseline (base_y)
                // when adjacent values include 0 (no data).
                float v0 = static_cast<float>(day_values[std::max(0, i - 1)]);
                float v1 = static_cast<float>(day_values[i]);
                float v2 = static_cast<float>(day_values[i + 1]);
                float v3 = static_cast<float>(day_values[std::min(num_days - 1, i + 2)]);

                float x1 = XPos(i);
                float x2 = XPos(i + 1);

                for (int s = 0; s < kSubDiv; ++s) {
                    float t = static_cast<float>(s) / kSubDiv;
                    float val = CatmullRom(v0, v1, v2, v3, t);
                    val = std::max(0.0f, val);  // clamp to non-negative
                    float cur_y = base_y - plot_h * (val / max_val);
                    float cur_x = x1 + (x2 - x1) * t;
                    curve_pts.push_back({cur_x, cur_y});
                }
            }
            // Last point
            {
                int last = num_days - 1;
                curve_pts.push_back({XPos(last), YPos(day_values[last])});
            }

            // ── Area fill under curve ──
            // Tessellate each adjacent pair of curve points into 2 triangles
            float area_alpha = 0.15f;
            auto fill_color = media_engine::Color{255, 140, 0, static_cast<uint8_t>(255 * area_alpha)}; // orange with alpha
            for (size_t i = 0; i + 1 < curve_pts.size(); ++i) {
                auto& p1 = curve_pts[i];
                auto& p2 = curve_pts[i + 1];
                // Triangle 1: (p1, p2, base_right)
                media_engine::DrawList::FilledTriangle(
                    p1.x, p1.y, p2.x, p2.y, p2.x, base_y, fill_color);
                // Triangle 2: (p1, base_right, base_left)
                media_engine::DrawList::FilledTriangle(
                    p1.x, p1.y, p2.x, base_y, p1.x, base_y, fill_color);
            }

            // ── Draw curve line ──
            for (size_t i = 0; i + 1 < curve_pts.size(); ++i) {
                media_engine::DrawList::Line(
                    curve_pts[i].x, curve_pts[i].y,
                    curve_pts[i + 1].x, curve_pts[i + 1].y,
                    media_engine::Colors::Orange, 2.0f);
            }

            // ── Data point dots ──
            for (int i = 0; i < num_days; ++i) {
                if (day_values[i] > 0) {
                    media_engine::DrawList::CircleFilled(
                        XPos(i), YPos(day_values[i]), 3.0f, media_engine::Colors::Orange);
                }
            }
        }

        // ── X-axis date labels ──
        float step = (num_days > 1) ? plot_w / (num_days - 1) : 0;
        int label_step = (num_days > 14) ? num_days / 7 : 1;
        if (label_step < 1) label_step = 1;
        for (int i = 0; i < num_days; ++i) {
            if (i % label_step == 0 || i == num_days - 1) {
                float x = plot_x + i * step;
                auto date = DateNDaysAgo(start_offset - i);
                media_engine::DrawList::Text(x - 14.0f, plot_y + plot_h + 4.0f,
                    media_engine::Colors::Gray55, ShortDate(date).c_str());
            }
        }

        // ── Axes ──
        media_engine::DrawList::Line(plot_x, plot_y, plot_x, base_y,
            media_engine::Colors::Gray63, 1.0f);
        media_engine::DrawList::Line(plot_x, base_y, plot_x + plot_w, base_y,
            media_engine::Colors::Gray63, 1.0f);

        // ── Title (rendered from pre-computed chart_title above) ──
        media_engine::DrawList::Text(chart_x + 14.0f, chart_y + 6.0f,
            media_engine::Colors::OrangeDeep, chart_title);
    } else {
        media_engine::DrawList::Text(chart_x + chart_w * 0.5f - 50.0f,
            chart_y + chart_area_h * 0.5f - 8.0f,
            media_engine::Colors::Gray55, "No usage data yet");
    }
}

} // namespace prosophor
